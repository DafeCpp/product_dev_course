"""Sensors API and repository against a real database: filters, errors, status summary, heartbeat."""
from __future__ import annotations

import uuid
from datetime import UTC, datetime, timedelta

import asyncpg
import pytest

from experiment_service.core.exceptions import (
    DuplicateResourceError,
    EmptyUpdateError,
    NotFoundError,
)
from experiment_service.domain.dto import SensorCreateDTO, SensorUpdateDTO
from experiment_service.domain.enums import SensorStatus
from experiment_service.repositories import SensorRepository
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


@pytest.fixture
def repo(db_pool) -> SensorRepository:
    return SensorRepository(db_pool)


def sensor_dto(project_id, name: str = "s", **extra) -> SensorCreateDTO:
    return SensorCreateDTO(
        project_id=project_id, name=name, type="thermo", input_unit="mV", display_unit="C", **extra
    )


async def create_sensor(client, project_id, headers, name: str, **extra) -> dict:
    response = await client.post(
        "/api/v1/sensors",
        json={
            "project_id": str(project_id),
            "name": name,
            "type": "thermo",
            "input_unit": "mV",
            "display_unit": "C",
            **extra,
        },
        headers=headers,
    )
    assert response.status == 201, await response.text()
    return (await response.json())["sensor"]


# --- routes -----------------------------------------------------------------------------------------------


async def test_register_sensor_validation_errors(service_client, project_id, headers):
    response = await service_client.post(
        "/api/v1/sensors", json={"project_id": str(project_id), "name": "no-type"}, headers=headers
    )
    assert response.status == 400


async def test_list_sensors_filters(service_client, project_id, headers):
    first = await create_sensor(service_client, project_id, headers, "alpha")
    second = await create_sensor(service_client, project_id, headers, "beta", status="active")
    query = f"project_id={project_id}"

    response = await service_client.get(f"/api/v1/sensors?{query}&status=active", headers=headers)
    assert response.status == 200
    assert [s["id"] for s in (await response.json())["sensors"]] == [second["id"]]

    now = datetime.now(UTC)
    after = (now - timedelta(minutes=5)).isoformat().replace("+", "%2B")
    before = (now + timedelta(minutes=5)).isoformat().replace("+", "%2B")
    response = await service_client.get(
        f"/api/v1/sensors?{query}&created_after={after}&created_before={before}", headers=headers
    )
    assert response.status == 200
    assert {s["id"] for s in (await response.json())["sensors"]} == {first["id"], second["id"]}

    response = await service_client.get(
        f"/api/v1/sensors?{query}&created_after={before}", headers=headers
    )
    assert (await response.json())["sensors"] == []


async def test_list_sensors_uses_active_project_filters(service_client, project_id, headers):
    created = await create_sensor(service_client, project_id, headers, "alpha", status="active")
    response = await service_client.get("/api/v1/sensors?status=active", headers=headers)
    assert response.status == 200
    assert [s["id"] for s in (await response.json())["sensors"]] == [created["id"]]


async def test_list_sensors_without_any_project_is_empty(service_client):
    response = await service_client.get("/api/v1/sensors", headers=make_headers(None))
    assert response.status == 200
    assert (await response.json())["sensors"] == []


async def test_list_sensors_with_project_ids_header_skips_invalid_parts(service_client, project_id, headers):
    created = await create_sensor(service_client, project_id, headers, "alpha")
    header = f" ,not-a-uuid, {project_id} ,"
    response = await service_client.get(
        "/api/v1/sensors", headers={**make_headers(None), "X-Project-Ids": header}
    )
    assert response.status == 200
    assert [s["id"] for s in (await response.json())["sensors"]] == [created["id"]]

    response = await service_client.get(
        "/api/v1/sensors", headers={**make_headers(None), "X-Project-Ids": "   "}
    )
    assert (await response.json())["sensors"] == []


async def test_update_sensor_errors_and_success(service_client, project_id, headers):
    first = await create_sensor(service_client, project_id, headers, "alpha")
    await create_sensor(service_client, project_id, headers, "beta")
    url = f"/api/v1/sensors/{first['id']}?project_id={project_id}"

    response = await service_client.patch(url, json={"unknown": 1}, headers=headers)
    assert response.status == 400

    response = await service_client.patch(url, json={"name": "beta"}, headers=headers)
    assert response.status == 409

    response = await service_client.patch(url, json={"active_profile_id": str(uuid.uuid4())}, headers=headers)
    assert response.status == 404

    response = await service_client.patch(url, json={}, headers=headers)
    assert response.status == 400

    response = await service_client.patch(
        url, json={"name": "gamma", "status": "active", "calibration_notes": "ok"}, headers=headers
    )
    assert response.status == 200
    body = await response.json()
    assert (body["name"], body["status"], body["calibration_notes"]) == ("gamma", "active", "ok")

    response = await service_client.patch(
        f"/api/v1/sensors/{uuid.uuid4()}?project_id={project_id}", json={"name": "x"}, headers=headers
    )
    assert response.status == 404


async def test_status_summary_route(service_client, project_id, headers):
    await create_sensor(service_client, project_id, headers, "alpha")
    response = await service_client.get(f"/api/v1/sensors/status-summary?project_id={project_id}", headers=headers)
    assert response.status == 200
    summary = await response.json()
    assert (summary["total"], summary["offline"]) == (1, 1)


async def test_heartbeat_history_route(service_client, project_id, headers):
    sensor = await create_sensor(service_client, project_id, headers, "alpha")
    url = f"/api/v1/sensors/{sensor['id']}/heartbeat-history?project_id={project_id}"

    response = await service_client.get(url, headers=headers)
    assert response.status == 200
    assert await response.json() == {"sensor_id": sensor["id"], "timestamps": [], "count": 0}

    for bad in ("0", "1441", "abc"):
        response = await service_client.get(f"{url}&minutes={bad}", headers=headers)
        assert response.status == 400

    response = await service_client.get(
        f"/api/v1/sensors/{uuid.uuid4()}/heartbeat-history?project_id={project_id}", headers=headers
    )
    assert response.status == 404


# --- repository -------------------------------------------------------------------------------------------


async def test_repository_create_duplicate_name(repo, project_id):
    await repo.create(sensor_dto(project_id, "dup"), token_hash=b"h1", token_preview="1111")
    with pytest.raises(DuplicateResourceError):
        await repo.create(sensor_dto(project_id, "dup"), token_hash=b"h2", token_preview="2222")


async def test_repository_list_filters_and_totals(repo, project_id):
    await repo.create(sensor_dto(project_id, "a", status=SensorStatus.ACTIVE), token_hash=b"a", token_preview="aaaa")
    await repo.create(sensor_dto(project_id, "b"), token_hash=b"b", token_preview="bbbb")
    now = datetime.now(UTC)
    window = {"created_after": now - timedelta(minutes=5), "created_before": now + timedelta(minutes=5)}

    items, total = await repo.list_by_project(project_id, status=SensorStatus.ACTIVE, **window)
    assert ([s.name for s in items], total) == (["a"], 1)

    items, total = await repo.list_by_project(None, status=SensorStatus.ACTIVE, **window)
    assert "a" in {s.name for s in items} and total >= 1

    items, total = await repo.list_by_project(None)
    assert total >= 2

    # offset beyond the last row: no window total, so the count query is used.
    items, total = await repo.list_by_project(project_id, offset=10)
    assert (items, total) == ([], 2)
    items, total = await repo.list_by_project(None, offset=10_000)
    assert items == [] and total >= 2

    items, total = await repo.list_by_projects([], limit=10)
    assert (items, total) == ([], 0)
    items, total = await repo.list_by_projects([project_id], limit=10)
    assert total == 2
    items, total = await repo.list_by_projects([project_id], offset=10)
    assert (items, total) == ([], 2)
    assert await repo._count_by_projects([]) == 0


async def test_repository_update_paths(repo, project_id):
    sensor = await repo.create(sensor_dto(project_id, "a"), token_hash=b"a", token_preview="aaaa")
    await repo.create(sensor_dto(project_id, "b"), token_hash=b"b", token_preview="bbbb")

    with pytest.raises(EmptyUpdateError):
        await repo.update(project_id, sensor.id, SensorUpdateDTO())
    with pytest.raises(DuplicateResourceError):
        await repo.update(project_id, sensor.id, SensorUpdateDTO(name="b"))
    with pytest.raises(NotFoundError):
        await repo.update(project_id, sensor.id, SensorUpdateDTO(active_profile_id=uuid.uuid4()))
    updated = await repo.update(project_id, sensor.id, SensorUpdateDTO(status=SensorStatus.ACTIVE))
    assert updated.status == SensorStatus.ACTIVE


async def test_repository_vanished_sensor_raises_not_found(repo, project_id, monkeypatch):
    sensor = await repo.create(sensor_dto(project_id, "a"), token_hash=b"a", token_preview="aaaa")

    async def present(*args, **kwargs):
        return sensor

    async def gone(*args, **kwargs):
        return None

    # The membership check passes, but the row disappears before the write.
    monkeypatch.setattr(repo, "get", present)
    monkeypatch.setattr(repo, "_fetchrow", gone)
    with pytest.raises(NotFoundError):
        await repo.update(project_id, sensor.id, SensorUpdateDTO(name="x"))
    with pytest.raises(NotFoundError):
        await repo.delete(project_id, sensor.id)
    with pytest.raises(NotFoundError):
        await repo.rotate_token(project_id, sensor.id, token_hash=b"n", token_preview="nnnn")
    with pytest.raises(NotFoundError):
        await repo.set_active_profile(project_id, sensor.id, None)
    assert await repo.has_active_capture_sessions(project_id, sensor.id) is False
    assert await repo.get_status_summary(project_id) == {"online": 0, "delayed": 0, "offline": 0, "total": 0}
    assert await repo._count_by_project(project_id) == 0


async def test_repository_tokens_and_profile(repo, project_id):
    sensor = await repo.create(sensor_dto(project_id, "a"), token_hash=b"old", token_preview="oooo")
    assert (await repo.get_by_token(sensor.id, b"old")).id == sensor.id
    rotated = await repo.rotate_token(project_id, sensor.id, token_hash=b"new", token_preview="nnnn")
    assert rotated.token_preview == "nnnn"
    with pytest.raises(NotFoundError):
        await repo.get_by_token(sensor.id, b"old")
    cleared = await repo.set_active_profile(project_id, sensor.id, None)
    assert cleared.active_profile_id is None


async def test_repository_status_summary_counts(repo, db_pool, project_id):
    online = await repo.create(sensor_dto(project_id, "on"), token_hash=b"1", token_preview="1111")
    stale = await repo.create(sensor_dto(project_id, "off"), token_hash=b"2", token_preview="2222")
    await repo.create(sensor_dto(project_id, "never"), token_hash=b"3", token_preview="3333")
    await db_pool.execute("UPDATE sensors SET last_heartbeat = now() WHERE id = $1", online.id)
    await db_pool.execute("UPDATE sensors SET last_heartbeat = now() - INTERVAL '30 days' WHERE id = $1", stale.id)

    summary = await repo.get_status_summary(project_id)
    assert summary["total"] == 3
    assert summary["online"] == 1
    assert summary["offline"] == 2


async def test_repository_sensor_projects_membership(repo, db_pool, project_id):
    sensor = await repo.create(sensor_dto(project_id, "a"), token_hash=b"a", token_preview="aaaa")
    other = uuid.uuid4()
    assert await repo.get_sensor_projects(sensor.id) == [project_id]

    await repo.add_sensor_project(sensor.id, other)
    await repo.add_sensor_project(sensor.id, other)
    assert set(await repo.get_sensor_projects(sensor.id)) == {project_id, other}

    await repo.remove_sensor_project(sensor.id, other)
    with pytest.raises(NotFoundError):
        await repo.remove_sensor_project(sensor.id, other)
    with pytest.raises(NotFoundError):
        await repo.add_sensor_project(uuid.uuid4(), other)

    # Older databases may lack sensor_projects rows: fall back to the primary project.
    await db_pool.execute("DELETE FROM sensor_projects WHERE sensor_id = $1", sensor.id)
    assert await repo.get_sensor_projects(sensor.id) == [project_id]
    assert await repo.get_sensor_projects(uuid.uuid4()) == []

    with pytest.raises(NotFoundError):
        await repo.get_by_id(uuid.uuid4())
