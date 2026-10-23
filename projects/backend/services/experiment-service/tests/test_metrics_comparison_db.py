"""Run metrics and run comparison against a real database (repository SQL, service, routes)."""
from __future__ import annotations

import csv
import io
import json
import uuid
from datetime import UTC, datetime

import pytest

from tests.utils import make_headers

NOW = datetime(2026, 1, 1, tzinfo=UTC).isoformat()


@pytest.fixture
def project_id() -> uuid.UUID:
    return uuid.uuid4()


@pytest.fixture
def headers(project_id):
    return make_headers(project_id)


@pytest.fixture
async def experiment_id(service_client, project_id, headers) -> str:
    response = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "Compare"},
        headers=headers,
    )
    assert response.status == 201
    return (await response.json())["id"]


@pytest.fixture
async def make_run(service_client, experiment_id, headers):
    async def create(name: str) -> str:
        response = await service_client.post(
            f"/api/v1/experiments/{experiment_id}/runs", json={"name": name}, headers=headers
        )
        assert response.status == 201
        return (await response.json())["id"]

    return create


async def log(client, headers, run_id: str, name: str, values: list[float]) -> None:
    points = [
        {"name": name, "step": step, "value": value, "timestamp": NOW}
        for step, value in enumerate(values)
    ]
    response = await client.post(f"/api/v1/runs/{run_id}/metrics", json={"metrics": points}, headers=headers)
    assert response.status == 202


@pytest.fixture
async def two_runs(service_client, headers, make_run):
    first = await make_run("first")
    second = await make_run("second")
    await log(service_client, headers, first, "loss", [4.0, 3.0, 2.0, 1.0])
    await log(service_client, headers, first, "acc", [0.1, 0.2, 0.3, 0.4])
    await log(service_client, headers, second, "loss", [8.0, 6.0, 4.0, 2.0])
    return first, second


# --- single-run metric queries ------------------------------------------------------------------


async def test_metric_series_filters_by_name_step_range_and_order(service_client, headers, two_runs):
    first, _ = two_runs

    by_names = await service_client.get(
        f"/api/v1/runs/{first}/metrics?names=loss,acc&from_step=1&to_step=2&order=desc", headers=headers
    )
    assert by_names.status == 200
    body = await by_names.json()
    assert body["total"] == 4
    steps = {series["name"]: [p["step"] for p in series["points"]] for series in body["series"]}
    assert steps == {"acc": [2, 1], "loss": [2, 1]}

    single = await service_client.get(f"/api/v1/runs/{first}/metrics?name=acc&limit=2&offset=1", headers=headers)
    payload = await single.json()
    assert payload["total"] == 4
    assert [p["step"] for p in payload["series"][0]["points"]] == [1, 2]


async def test_metric_summary_reports_last_and_aggregates(service_client, headers, two_runs):
    first, _ = two_runs

    everything = await (await service_client.get(f"/api/v1/runs/{first}/metrics/summary", headers=headers)).json()
    by_name = {m["name"]: m for m in everything["metrics"]}
    assert by_name["loss"]["last_value"] == 1.0
    assert by_name["loss"]["min_value"] == 1.0
    assert by_name["loss"]["max_value"] == 4.0
    assert by_name["loss"]["avg_value"] == 2.5

    only_acc = await (
        await service_client.get(f"/api/v1/runs/{first}/metrics/summary?names=acc", headers=headers)
    ).json()
    assert [m["name"] for m in only_acc["metrics"]] == ["acc"]


async def test_metric_aggregations_bucket_the_steps(service_client, headers, two_runs):
    first, _ = two_runs

    response = await service_client.get(
        f"/api/v1/runs/{first}/metrics/aggregations?names=loss&bucket_size=2&from_step=0&to_step=3",
        headers=headers,
    )
    assert response.status == 200
    buckets = (await response.json())["series"][0]["buckets"]
    assert [(b["step_from"], b["step_to"], b["count"]) for b in buckets] == [(0, 1, 2), (2, 3, 2)]
    assert buckets[0]["avg"] == 3.5

    auto = await service_client.get(
        f"/api/v1/runs/{first}/metrics/aggregations?names=loss&from_step=0&to_step=900", headers=headers
    )
    assert (await auto.json())["bucket_size"] == 3


# --- comparison -----------------------------------------------------------------------------------


async def compare(client, headers, experiment_id, run_ids, names, **extra):
    body = {"run_ids": run_ids, "metric_names": names, **extra}
    response = await client.post(f"/api/v1/experiments/{experiment_id}/compare", json=body, headers=headers)
    return response


async def test_compare_returns_summary_and_series_per_run(service_client, headers, experiment_id, two_runs):
    first, second = two_runs

    response = await compare(service_client, headers, experiment_id, [first, second], ["loss", "acc"])
    assert response.status == 200
    runs = {r["run_id"]: r for r in (await response.json())["runs"]}

    assert runs[first]["run_name"] == "first"
    assert runs[first]["metrics"]["loss"]["summary"]["last_value"] == 1.0
    assert runs[first]["metrics"]["loss"]["summary"]["count"] == 4
    assert len(runs[first]["metrics"]["loss"]["series"]) == 4
    # A metric never logged for a run is omitted instead of returned empty.
    assert "acc" not in runs[second]["metrics"]


async def test_compare_respects_the_step_window(service_client, headers, experiment_id, two_runs):
    first, second = two_runs

    response = await compare(
        service_client, headers, experiment_id, [first, second], ["loss"], from_step=1, to_step=2
    )
    runs = {r["run_id"]: r for r in (await response.json())["runs"]}
    assert [p["step"] for p in runs[first]["metrics"]["loss"]["series"]] == [1, 2]


async def test_compare_downsamples_long_series(service_client, headers, experiment_id, make_run):
    first = await make_run("long-1")
    second = await make_run("long-2")
    values = [float(i) for i in range(260)]
    await log(service_client, headers, first, "loss", values)
    await log(service_client, headers, second, "loss", values)

    response = await compare(
        service_client, headers, experiment_id, [first, second], ["loss"], max_points_per_series=100
    )
    series = (await response.json())["runs"][0]["metrics"]["loss"]["series"]
    assert 0 < len(series) <= 130
    assert series[0]["step"] == 0

    # The step window is applied before bucketing as well.
    windowed = await compare(
        service_client,
        headers,
        experiment_id,
        [first, second],
        ["loss"],
        max_points_per_series=100,
        from_step=10,
        to_step=250,
    )
    steps = [p["step"] for p in (await windowed.json())["runs"][0]["metrics"]["loss"]["series"]]
    assert steps and min(steps) >= 10 and max(steps) <= 250


async def test_compare_rejects_runs_from_another_experiment(service_client, headers, experiment_id, two_runs):
    first, _ = two_runs
    response = await compare(service_client, headers, experiment_id, [first, str(uuid.uuid4())], ["loss"])
    assert response.status == 404


@pytest.mark.parametrize(
    "body",
    [
        {"run_ids": "nope", "metric_names": ["loss"]},
        {"run_ids": ["not-a-uuid", "x"], "metric_names": ["loss"]},
        {"run_ids": [str(uuid.uuid4())] * 2, "metric_names": []},
        {"run_ids": [str(uuid.uuid4())] * 2, "metric_names": "loss"},
        {"run_ids": [str(uuid.uuid4())] * 2, "metric_names": ["loss"], "from_step": "soon"},
        {"run_ids": [str(uuid.uuid4())] * 2, "metric_names": ["loss"], "max_points_per_series": "many"},
        {"run_ids": [str(uuid.uuid4())], "metric_names": ["loss"]},
    ],
)
async def test_compare_post_rejects_bad_bodies(service_client, headers, experiment_id, body):
    response = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/compare", json=body, headers=headers
    )
    assert response.status == 400


async def test_compare_get_validates_query_and_serves_results(service_client, headers, experiment_id, two_runs):
    first, second = two_runs
    base = f"/api/v1/experiments/{experiment_id}/compare"

    ok = await service_client.get(
        f"{base}?run_ids={first},{second}&names=loss&max_points=50&from_step=0&to_step=3", headers=headers
    )
    assert ok.status == 200

    for query in (
        "",
        f"run_ids={first},{second}",
        f"run_ids={first},{second}&names=,",
        f"run_ids={first},nope&names=loss",
        f"run_ids={first},{second}&names=loss&from_step=x",
        f"run_ids={first}&names=loss",
    ):
        response = await service_client.get(f"{base}?{query}", headers=headers)
        assert response.status == 400, query

    missing = await service_client.get(f"{base}?run_ids={first},{uuid.uuid4()}&names=loss", headers=headers)
    assert missing.status == 404


async def test_compare_export_as_csv_and_json(service_client, headers, experiment_id, two_runs):
    first, second = two_runs
    base = f"/api/v1/experiments/{experiment_id}/compare/export?run_ids={first},{second}&names=loss"

    as_csv = await service_client.get(base, headers=headers)
    assert as_csv.status == 200
    assert as_csv.headers["Content-Disposition"].endswith('.csv"')
    rows = list(csv.reader(io.StringIO(await as_csv.text())))
    assert rows[0] == ["run_id", "run_name", "metric_name", "step", "value"]
    assert len(rows) == 1 + 4 + 4

    as_json = await service_client.get(f"{base}&format=json&max_points=999999", headers=headers)
    assert as_json.status == 200
    assert as_json.headers["Content-Disposition"].endswith('.json"')
    assert json.loads(await as_json.text())["metric_names"] == ["loss"]


@pytest.mark.parametrize(
    "query",
    [
        "format=xml&run_ids=a&names=loss",
        "names=loss",
        "run_ids={a},{b}",
        "run_ids={a},{b}&names=,",
        "run_ids={a},nope&names=loss",
        "run_ids={a},{b}&names=loss&from_step=x",
    ],
)
async def test_compare_export_rejects_bad_queries(service_client, headers, experiment_id, query):
    query = query.format(a=uuid.uuid4(), b=uuid.uuid4())
    response = await service_client.get(
        f"/api/v1/experiments/{experiment_id}/compare/export?{query}", headers=headers
    )
    assert response.status == 400


async def test_compare_export_reports_missing_runs(service_client, headers, experiment_id):
    response = await service_client.get(
        f"/api/v1/experiments/{experiment_id}/compare/export?run_ids={uuid.uuid4()},{uuid.uuid4()}&names=loss",
        headers=headers,
    )
    assert response.status == 404


# --- metric route validation ----------------------------------------------------------------------


@pytest.mark.parametrize(
    "query",
    ["from_step=x", "to_step=x", "limit=x", "offset=x", "order=sideways"],
)
async def test_metric_query_rejects_bad_parameters(service_client, headers, two_runs, query):
    first, _ = two_runs
    response = await service_client.get(f"/api/v1/runs/{first}/metrics?{query}", headers=headers)
    assert response.status == 400


@pytest.mark.parametrize(
    "path",
    ["metrics", "metrics/summary", "metrics/aggregations?names=loss"],
)
async def test_metric_reads_report_unknown_runs(service_client, headers, path):
    response = await service_client.get(f"/api/v1/runs/{uuid.uuid4()}/{path}", headers=headers)
    assert response.status == 404


async def test_metric_ingest_rejects_malformed_points_and_oversized_batches(service_client, headers, two_runs):
    first, _ = two_runs
    url = f"/api/v1/runs/{first}/metrics"

    malformed = await service_client.post(url, json={"metrics": [{"name": "loss"}]}, headers=headers)
    assert malformed.status == 400

    from experiment_service.services.metrics import MetricsService

    too_many = [{"name": "loss", "step": i, "value": 1.0, "timestamp": NOW} for i in range(3)]
    original = MetricsService.BATCH_LIMIT
    MetricsService.BATCH_LIMIT = 2
    try:
        response = await service_client.post(url, json={"metrics": too_many}, headers=headers)
    finally:
        MetricsService.BATCH_LIMIT = original
    assert response.status == 400
    assert "Batch too large" in await response.text()


async def test_metric_aggregations_reject_bad_parameters(service_client, headers, two_runs):
    first, _ = two_runs
    base = f"/api/v1/runs/{first}/metrics/aggregations"
    for query in ("names=,", "names=loss&from_step=x", "names=loss&to_step=x"):
        response = await service_client.get(f"{base}?{query}", headers=headers)
        assert response.status == 400, query
