"""Experiments and runs API against a real database: filters, errors, bulk operations, run sensors."""
from __future__ import annotations

import uuid
from datetime import UTC, datetime, timedelta

import asyncpg
import pytest

from experiment_service.repositories import RunRepository
from experiment_service.services.idempotency import IDEMPOTENCY_HEADER
from tests.utils import make_headers


@pytest.fixture
def project_id() -> uuid.UUID:
    return uuid.uuid4()


@pytest.fixture
def headers(project_id):
    return make_headers(project_id)


@pytest.fixture
async def db_pool(pgsql):
    pool = await asyncpg.create_pool(dsn=pgsql["experiment_service"].conninfo.get_uri())
    try:
        yield pool
    finally:
        await pool.close()


async def create_experiment(client, project_id, headers, name: str, **extra) -> dict:
    response = await client.post(
        "/api/v1/experiments", json={"project_id": str(project_id), "name": name, **extra}, headers=headers
    )
    assert response.status == 201, await response.text()
    return await response.json()


async def create_run(client, experiment_id: str, headers, name: str = "run", **extra) -> dict:
    response = await client.post(
        f"/api/v1/experiments/{experiment_id}/runs", json={"name": name, **extra}, headers=headers
    )
    assert response.status == 201, await response.text()
    return await response.json()


@pytest.fixture
async def experiment(service_client, project_id, headers) -> dict:
    return await create_experiment(service_client, project_id, headers, "Main")


# --- experiments ----------------------------------------------------------------------------------------


async def test_experiment_list_filters(service_client, project_id, headers):
    first = await create_experiment(service_client, project_id, headers, "alpha", tags=["x", "y"])
    second = await create_experiment(service_client, project_id, headers, "beta", tags=["y"])
    await service_client.post(f"/api/v1/experiments/{second['id']}/archive", headers=headers)

    async def listed(query: str) -> list[str]:
        response = await service_client.get(f"/api/v1/experiments?{query}", headers=headers)
        assert response.status == 200, query
        return sorted(e["name"] for e in (await response.json())["experiments"])

    assert await listed("") == ["alpha", "beta"]
    assert await listed("status=archived") == ["beta"]
    assert await listed("tags=x") == ["alpha"]
    assert await listed("tags=y&status=draft") == ["alpha"]
    day = timedelta(days=1)
    now = datetime.now(UTC)
    assert await listed(f"created_after={(now + day).isoformat().replace('+00:00', 'Z')}") == []
    assert await listed(f"created_before={(now + day).isoformat().replace('+00:00', 'Z')}") == ["alpha", "beta"]
    assert await listed(f"project_id={project_id}&limit=1") in (["alpha"], ["beta"])
    assert first["id"] != second["id"]


async def test_experiment_list_needs_a_project(service_client):
    response = await service_client.get("/api/v1/experiments", headers=make_headers(None))
    assert response.status == 400
    assert "project_id is required" in await response.text()


@pytest.mark.parametrize("query", ["status=nope", "created_after=yesterday", "created_before=soon"])
async def test_experiment_list_rejects_bad_filters(service_client, headers, query):
    response = await service_client.get(f"/api/v1/experiments?{query}", headers=headers)
    assert response.status == 400


async def test_experiment_search(service_client, project_id, headers):
    await create_experiment(service_client, project_id, headers, "Wind tunnel", description="subsonic")
    await create_experiment(service_client, project_id, headers, "Other")

    by_name = await (await service_client.get("/api/v1/experiments/search?q=tunnel", headers=headers)).json()
    by_description = await (await service_client.get("/api/v1/experiments/search?q=SUBSONIC", headers=headers)).json()
    missing_query = await service_client.get("/api/v1/experiments/search?q=%20", headers=headers)

    assert [e["name"] for e in by_name["experiments"]] == ["Wind tunnel"]
    assert by_description["total"] == 1
    assert missing_query.status == 400


async def test_experiment_errors(service_client, project_id, headers, experiment):
    ghost = uuid.uuid4()
    for method, path in (("GET", f"/api/v1/experiments/{ghost}"), ("DELETE", f"/api/v1/experiments/{ghost}")):
        response = await service_client.request(method, path, headers=headers)
        assert response.status == 404
    response = await service_client.post(f"/api/v1/experiments/{ghost}/archive", headers=headers)
    assert response.status == 404
    patch_ghost = await service_client.patch(f"/api/v1/experiments/{ghost}", json={"name": "x"}, headers=headers)
    assert patch_ghost.status == 404

    bad_patch = await service_client.patch(
        f"/api/v1/experiments/{experiment['id']}", json={"status": "nonsense"}, headers=headers
    )
    assert bad_patch.status == 400

    other = await create_experiment(service_client, project_id, headers, "Second")
    clash = await service_client.patch(
        f"/api/v1/experiments/{other['id']}", json={"name": experiment["name"]}, headers=headers
    )
    assert clash.status == 409


async def test_experiment_lifecycle(service_client, headers, experiment):
    url = f"/api/v1/experiments/{experiment['id']}"

    renamed = await service_client.patch(url, json={"name": "Renamed", "tags": ["z"]}, headers=headers)
    assert (await renamed.json())["name"] == "Renamed"
    response = await service_client.get(url, headers=headers)
    assert response.status == 200

    archived = await service_client.post(f"{url}/archive", headers=headers)
    assert (await archived.json())["status"] == "archived"

    response = await service_client.delete(url, headers=headers)
    assert response.status == 204
    response = await service_client.get(url, headers=headers)
    assert response.status == 404


# --- runs: list / create ---------------------------------------------------------------------------------


async def test_run_list_filters(service_client, experiment, headers):
    first = await create_run(service_client, experiment["id"], headers, "one")
    second = await create_run(service_client, experiment["id"], headers, "two")
    await bulk_tags(service_client, headers, {"run_ids": [first["id"]], "set_tags": ["fast"]})
    await bulk_tags(service_client, headers, {"run_ids": [second["id"]], "set_tags": ["slow"]})
    base = f"/api/v1/experiments/{experiment['id']}/runs"

    async def listed(query: str) -> list[str]:
        response = await service_client.get(f"{base}?{query}", headers=headers)
        assert response.status == 200, query
        return sorted(r["name"] for r in (await response.json())["runs"])

    assert await listed("") == ["one", "two"]
    assert await listed("tags=fast") == ["one"]
    assert await listed("status=running") == []
    assert await listed("created_after=2000-01-01T00:00:00Z&created_before=2999-01-01T00:00:00Z") == ["one", "two"]
    assert first["status"] == "draft"

    for bad in ("status=nope", "created_after=x"):
        response = await service_client.get(f"{base}?{bad}", headers=headers)
        assert response.status == 400
    ghost = await service_client.get(f"/api/v1/experiments/{uuid.uuid4()}/runs", headers=headers)
    assert ghost.status == 404


async def test_run_create_validation_and_unknown_experiment(service_client, experiment, headers):
    bad = await service_client.post(
        f"/api/v1/experiments/{experiment['id']}/runs", json={"unknown_field": 1}, headers=headers
    )
    assert bad.status == 400
    ghost = await service_client.post(
        f"/api/v1/experiments/{uuid.uuid4()}/runs", json={"name": "x"}, headers=headers
    )
    assert ghost.status == 404


async def test_run_create_is_idempotent(service_client, experiment, headers):
    url = f"/api/v1/experiments/{experiment['id']}/runs"
    keyed = {**headers, IDEMPOTENCY_HEADER: "run-key-1"}

    first = await service_client.post(url, json={"name": "once"}, headers=keyed)
    replay = await service_client.post(url, json={"name": "once"}, headers=keyed)
    conflict = await service_client.post(url, json={"name": "different"}, headers=keyed)

    assert first.status == 201
    assert replay.status == 201
    assert (await replay.json())["id"] == (await first.json())["id"]
    assert conflict.status == 409
    listed = await (await service_client.get(url, headers=headers)).json()
    assert listed["total"] == 1


async def test_run_create_in_non_draft_status_validates_the_transition(service_client, experiment, headers):
    running = await service_client.post(
        f"/api/v1/experiments/{experiment['id']}/runs",
        json={"name": "started", "status": "running"},
        headers=headers,
    )
    assert running.status == 201
    assert (await running.json())["status"] == "running"

    invalid = await service_client.post(
        f"/api/v1/experiments/{experiment['id']}/runs",
        json={"name": "weird", "status": "succeeded"},
        headers=headers,
    )
    assert invalid.status == 400


# --- runs: update / status -----------------------------------------------------------------------------------


async def test_run_status_changes_record_events(service_client, experiment, headers):
    run = await create_run(service_client, experiment["id"], headers)
    url = f"/api/v1/runs/{run['id']}"

    for status in ("running", "succeeded", "archived"):
        response = await service_client.patch(url, json={"status": status}, headers=headers)
        assert response.status == 200, status
        assert (await response.json())["status"] == status

    events = await (await service_client.get(f"{url}/events", headers=headers)).json()
    changes = [e["payload"] for e in events["events"] if e["event_type"] == "run.status_changed"]
    assert [(c["from"], c["to"]) for c in changes] == [
        ("draft", "running"),
        ("running", "succeeded"),
        ("succeeded", "archived"),
    ]
    assert events["total"] == 3


async def test_run_failed_status_and_plain_updates(service_client, experiment, headers):
    run = await create_run(service_client, experiment["id"], headers)
    url = f"/api/v1/runs/{run['id']}"

    await service_client.patch(url, json={"status": "running"}, headers=headers)
    failed = await service_client.patch(url, json={"status": "failed"}, headers=headers)
    assert (await failed.json())["status"] == "failed"

    renamed = await service_client.patch(url, json={"name": "renamed"}, headers=headers)
    assert (await renamed.json())["name"] == "renamed"


async def test_run_update_errors(service_client, experiment, headers):
    run = await create_run(service_client, experiment["id"], headers)
    ghost = uuid.uuid4()

    response = await service_client.get(f"/api/v1/runs/{ghost}", headers=headers)
    assert response.status == 404
    response = await service_client.patch(f"/api/v1/runs/{ghost}", json={"name": "x"}, headers=headers)
    assert response.status == 404
    ghost_status = await service_client.patch(
        f"/api/v1/runs/{ghost}", json={"status": "running"}, headers=headers
    )
    assert ghost_status.status == 404
    response = await service_client.patch(f"/api/v1/runs/{run['id']}", json={"status": "bogus"}, headers=headers)
    assert response.status == 400
    illegal = await service_client.patch(f"/api/v1/runs/{run['id']}", json={"status": "succeeded"}, headers=headers)
    assert illegal.status == 409
    response = await service_client.get(f"/api/v1/runs/{ghost}/events", headers=headers)
    assert response.status == 404


async def test_run_delete(service_client, experiment, headers):
    run = await create_run(service_client, experiment["id"], headers)

    response = await service_client.delete(f"/api/v1/runs/{run['id']}", headers=headers)
    assert response.status == 204
    response = await service_client.delete(f"/api/v1/runs/{run['id']}", headers=headers)
    assert response.status == 404


# --- runs: batch status ----------------------------------------------------------------------------------------


async def test_batch_status_updates_every_run_and_records_events(service_client, experiment, headers):
    first = await create_run(service_client, experiment["id"], headers, "a")
    second = await create_run(service_client, experiment["id"], headers, "b")

    response = await service_client.post(
        "/api/v1/runs:batch-status", json={"run_ids": [first["id"], second["id"]], "status": "running"}, headers=headers
    )
    assert response.status == 200
    assert {r["status"] for r in (await response.json())["runs"]} == {"running"}

    for run_id, status in ((first["id"], "succeeded"), (second["id"], "failed"), (first["id"], "archived")):
        again = await service_client.post(
            "/api/v1/runs:batch-status", json={"run_ids": [run_id], "status": status}, headers=headers
        )
        assert again.status == 200, status

    events = await (await service_client.get(f"/api/v1/runs/{first['id']}/events", headers=headers)).json()
    assert events["total"] == 3


@pytest.mark.parametrize(
    "body",
    [
        {"status": "running"},
        {"run_ids": [], "status": "running"},
        {"run_ids": ["{run}"]},
        {"run_ids": ["{run}"], "status": "bogus"},
        {"run_ids": ["not-a-uuid"], "status": "running"},
    ],
)
async def test_batch_status_rejects_bad_requests(service_client, experiment, headers, body):
    run = await create_run(service_client, experiment["id"], headers)
    body = {k: [v.replace("{run}", run["id"]) for v in val] if isinstance(val, list) else val for k, val in body.items()}

    response = await service_client.post("/api/v1/runs:batch-status", json=body, headers=headers)

    assert response.status == 400


async def test_batch_status_reports_unknown_runs(service_client, headers):
    response = await service_client.post(
        "/api/v1/runs:batch-status", json={"run_ids": [str(uuid.uuid4())], "status": "running"}, headers=headers
    )
    assert response.status == 404


# --- runs: bulk tags -----------------------------------------------------------------------------------------


async def bulk_tags(client, headers, body: dict):
    return await client.post("/api/v1/runs:bulk-tags", json=body, headers=headers)


async def test_bulk_tags_validation(service_client, experiment, headers):
    run = await create_run(service_client, experiment["id"], headers)
    ids = [run["id"]]

    response = await bulk_tags(service_client, headers, {"run_ids": []})
    assert response.status == 400
    response = await bulk_tags(service_client, headers, {"run_ids": ids})
    assert response.status == 400
    response = await bulk_tags(service_client, headers, {"run_ids": ids, "add_tags": []})
    assert response.status == 400
    response = await bulk_tags(service_client, headers, {"run_ids": ids, "set_tags": [], "add_tags": ["x"]})
    assert response.status == 400
    response = await bulk_tags(service_client, headers, {"run_ids": ids, "add_tags": "x"})
    assert response.status == 400
    response = await bulk_tags(service_client, headers, {"run_ids": ids, "add_tags": [1]})
    assert response.status == 400
    ghost = await bulk_tags(service_client, headers, {"run_ids": [str(uuid.uuid4())], "add_tags": ["x"]})
    assert ghost.status == 404
    ghost_set = await bulk_tags(service_client, headers, {"run_ids": [str(uuid.uuid4())], "set_tags": ["x"]})
    assert ghost_set.status == 404


async def test_bulk_tags_records_events(service_client, experiment, headers):
    run = await create_run(service_client, experiment["id"], headers)
    await bulk_tags(service_client, headers, {"run_ids": [run["id"]], "set_tags": ["old", "keep"]})

    response = await bulk_tags(
        service_client, headers, {"run_ids": [run["id"]], "add_tags": ["new"], "remove_tags": ["old"]}
    )
    assert sorted((await response.json())["runs"][0]["tags"]) == ["keep", "new"]

    cleared = await bulk_tags(service_client, headers, {"run_ids": [run["id"]], "set_tags": []})
    assert (await cleared.json())["runs"][0]["tags"] == []

    events = await (await service_client.get(f"/api/v1/runs/{run['id']}/events", headers=headers)).json()
    assert [e["event_type"] for e in events["events"]].count("run.tags_updated") == 3


# --- runs: sensors -----------------------------------------------------------------------------------------------


async def create_sensor(client, project_id, headers, name: str) -> str:
    response = await client.post(
        "/api/v1/sensors",
        json={"project_id": str(project_id), "name": name, "type": "thermocouple", "input_unit": "mV", "display_unit": "C"},
        headers={**headers, IDEMPOTENCY_HEADER: str(uuid.uuid4())},
    )
    assert response.status == 201
    return (await response.json())["sensor"]["id"]


async def test_run_sensor_attachment(service_client, project_id, experiment, headers):
    run = await create_run(service_client, experiment["id"], headers)
    sensor_id = await create_sensor(service_client, project_id, headers, "probe")
    base = f"/api/v1/runs/{run['id']}/sensors"

    attached = await service_client.post(f"{base}/{sensor_id}", json={"mode": "active"}, headers=headers)
    assert attached.status == 201
    assert (await attached.json())["mode"] == "active"

    listed = await (await service_client.get(base, headers=headers)).json()
    assert [s["sensor_id"] for s in listed["sensors"]] == [sensor_id]

    response = await service_client.delete(f"{base}/{sensor_id}", headers=headers)
    assert response.status == 204
    response = await service_client.delete(f"{base}/{sensor_id}", headers=headers)
    assert response.status == 404
    remaining = await service_client.get(base, headers=headers)
    assert (await remaining.json())["sensors"] == []
    response = await service_client.get(f"/api/v1/runs/{uuid.uuid4()}/sensors", headers=headers)
    assert response.status == 404


async def test_attaching_a_sensor_without_a_body_defaults_to_passive(service_client, project_id, experiment, headers):
    run = await create_run(service_client, experiment["id"], headers)
    sensor_id = await create_sensor(service_client, project_id, headers, "quiet")

    attached = await service_client.post(f"/api/v1/runs/{run['id']}/sensors/{sensor_id}", json={}, headers=headers)

    assert attached.status == 201
    assert (await attached.json())["mode"] == "passive"


# --- repository ------------------------------------------------------------------------------------------------------


async def test_run_repository_lists_by_project(service_client, db_pool, project_id, experiment, headers):
    for index in range(3):
        await create_run(service_client, experiment["id"], headers, f"run-{index}")
    repo = RunRepository(db_pool)

    runs, total = await repo.list_by_project(project_id, limit=2, offset=0)
    assert (len(runs), total) == (2, 3)

    beyond, total_beyond = await repo.list_by_project(project_id, limit=2, offset=10)
    assert beyond == []
    assert total_beyond == 3

    empty, empty_total = await repo.list_by_experiment(project_id, uuid.uuid4(), limit=5, offset=3)
    assert (empty, empty_total) == ([], 0)
    assert await repo.has_running_runs_for_experiment(project_id, uuid.UUID(experiment["id"])) is False
