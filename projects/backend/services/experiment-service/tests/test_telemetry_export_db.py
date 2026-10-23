"""Telemetry export: aggregation, filters, validation, batching and formatters against a real database."""
from __future__ import annotations

import csv
import io
import json
import uuid
from datetime import UTC, datetime

import asyncpg
import pytest

from experiment_service.api.routes import telemetry_export
from tests.test_api_telemetry_export import _insert_telemetry, _setup_context, _ts


@pytest.fixture
async def ctx(service_client, pgsql) -> dict:
    ctx = await _setup_context(service_client)
    await _insert_telemetry(
        pgsql,
        project_id=ctx["project_id"],
        sensor_id=ctx["sensor_uuid"],
        run_id=uuid.UUID(ctx["run_id"]),
        capture_session_id=uuid.UUID(ctx["capture_session_id"]),
        records=[
            {"timestamp": _ts(0), "raw_value": 1.0, "physical_value": 10.0, "conversion_status": "converted"},
            {"timestamp": _ts(1), "raw_value": 2.0, "physical_value": 20.0, "conversion_status": "converted"},
            {"timestamp": _ts(2), "raw_value": 3.0, "signal": "pressure", "late": True},
            {"timestamp": _ts(3), "raw_value": 4.0, "signal": "pressure"},
            {"timestamp": _ts(120), "raw_value": 5.0, "physical_value": 50.0, "conversion_status": "converted"},
        ],
    )
    return ctx


def session_url(ctx, **query) -> str:
    base = f"/api/v1/runs/{ctx['run_id']}/capture-sessions/{ctx['capture_session_id']}/telemetry/export"
    return f"{base}?project_id={ctx['project_id']}" + "".join(f"&{k}={v}" for k, v in query.items())


def run_url(ctx, **query) -> str:
    base = f"/api/v1/runs/{ctx['run_id']}/telemetry/export"
    return f"{base}?project_id={ctx['project_id']}" + "".join(f"&{k}={v}" for k, v in query.items())


# --- validation and permissions ---------------------------------------------------------------------------


@pytest.mark.parametrize("make_url", [session_url, run_url])
async def test_export_rejects_bad_parameters(service_client, ctx, make_url):
    for query in ({"format": "xml"}, {"raw_or_physical": "bogus"}, {"sensor_id": "nope"}):
        response = await service_client.get(make_url(ctx, **query), headers=ctx["headers"])
        assert response.status == 400, query


async def test_session_export_not_found_cases(service_client, ctx):
    headers = ctx["headers"]
    base = f"/api/v1/runs/{{}}/capture-sessions/{{}}/telemetry/export?project_id={ctx['project_id']}"

    response = await service_client.get(base.format(uuid.uuid4(), ctx["capture_session_id"]), headers=headers)
    assert response.status == 404
    response = await service_client.get(base.format(ctx["run_id"], uuid.uuid4()), headers=headers)
    assert response.status == 404


async def test_run_export_not_found_and_foreign_session(service_client, pgsql, ctx):
    headers = ctx["headers"]
    response = await service_client.get(
        f"/api/v1/runs/{uuid.uuid4()}/telemetry/export?project_id={ctx['project_id']}", headers=headers
    )
    assert response.status == 404

    response = await service_client.get(run_url(ctx, capture_session_id=uuid.uuid4()), headers=headers)
    assert response.status == 404

    # A session of another run of the same project must not be reachable through this run.
    # Only one session may be active per project, so retire the fixture's session first.
    conn = await asyncpg.connect(dsn=pgsql["experiment_service"].conninfo.get_uri())
    try:
        await conn.execute("UPDATE capture_sessions SET status = 'succeeded' WHERE id = $1", uuid.UUID(ctx["capture_session_id"]))
    finally:
        await conn.close()
    other = await service_client.post(
        f"/api/v1/experiments/{ctx['experiment_id']}/runs", json={"name": "other"}, headers=headers
    )
    other_run_id = (await other.json())["id"]
    other_session = await service_client.post(
        f"/api/v1/runs/{other_run_id}/capture-sessions",
        json={"ordinal_number": 2, "status": "running"},
        headers=headers,
    )
    assert other_session.status == 201, await other_session.text()
    other_session_id = (await other_session.json())["id"]
    response = await service_client.get(run_url(ctx, capture_session_id=other_session_id), headers=headers)
    assert response.status == 404


# --- raw export filters -----------------------------------------------------------------------------------


async def test_session_export_filters_and_modes(service_client, ctx):
    headers = ctx["headers"]

    response = await service_client.get(session_url(ctx, include_late="false", signal="pressure"), headers=headers)
    assert response.status == 200
    rows = list(csv.reader(io.StringIO(await response.text())))
    assert len(rows) == 2  # header + the one non-late pressure reading

    response = await service_client.get(
        session_url(ctx, sensor_id=ctx["sensor_id"], raw_or_physical="raw", format="json"), headers=headers
    )
    items = json.loads(await response.text())
    assert len(items) == 5
    assert all("raw_value" in item and "physical_value" not in item for item in items)

    response = await service_client.get(session_url(ctx, raw_or_physical="physical"), headers=headers)
    header = next(csv.reader(io.StringIO(await response.text())))
    assert "physical_value" in header and "raw_value" not in header


async def test_run_export_filters_by_session_and_signal(service_client, ctx):
    headers = ctx["headers"]
    response = await service_client.get(
        run_url(ctx, capture_session_id=ctx["capture_session_id"], include_late="false", signal="temp"),
        headers=headers,
    )
    assert response.status == 200
    assert len(list(csv.reader(io.StringIO(await response.text())))) == 4

    response = await service_client.get(run_url(ctx, format="json", raw_or_physical="physical"), headers=headers)
    items = json.loads(await response.text())
    assert len(items) == 5
    assert all("physical_value" in item and "raw_value" not in item for item in items)


async def test_export_without_rows_is_empty_json(service_client, ctx):
    response = await service_client.get(session_url(ctx, format="json", signal="missing"), headers=ctx["headers"])
    assert response.status == 200
    assert json.loads(await response.text()) == []


async def test_run_without_sessions_returns_empty_payloads(service_client):
    context = await _setup_context(service_client)
    experiment = context["experiment_id"]
    created = await service_client.post(
        f"/api/v1/experiments/{experiment}/runs", json={"name": "bare"}, headers=context["headers"]
    )
    run_id = (await created.json())["id"]
    url = f"/api/v1/runs/{run_id}/telemetry/export?project_id={context['project_id']}"

    response = await service_client.get(url + "&format=json", headers=context["headers"])
    assert await response.text() == "[]"
    response = await service_client.get(url, headers=context["headers"])
    assert await response.text() == ""


async def test_export_flushes_full_batches(service_client, ctx, monkeypatch):
    monkeypatch.setattr(telemetry_export, "STREAM_BATCH_SIZE", 2)
    response = await service_client.get(session_url(ctx), headers=ctx["headers"])
    assert response.status == 200
    assert len(list(csv.reader(io.StringIO(await response.text())))) == 6


# --- one-minute aggregation -------------------------------------------------------------------------------


@pytest.mark.parametrize("make_url", [session_url, run_url])
async def test_aggregated_export_csv_and_json(service_client, ctx, make_url):
    headers = ctx["headers"]

    response = await service_client.get(make_url(ctx, aggregation="1m"), headers=headers)
    assert response.status == 200
    rows = list(csv.reader(io.StringIO(await response.text())))
    assert rows[0][:5] == ["bucket", "sensor_id", "signal", "capture_session_id", "sample_count"]

    response = await service_client.get(
        make_url(ctx, aggregation="1m", format="json", signal="temp", sensor_id=ctx["sensor_id"]), headers=headers
    )
    items = json.loads(await response.text())
    assert all(item["signal"] == "temp" for item in items)


# --- formatters -------------------------------------------------------------------------------------------


def test_formatters_handle_missing_values():
    row = {
        "bucket": None,
        "timestamp": None,
        "sensor_id": uuid.uuid4(),
        "signal": None,
        "capture_session_id": None,
        "sample_count": None,
        "avg_raw": None,
        "min_raw": None,
        "max_raw": None,
        "avg_physical": None,
        "min_physical": None,
        "max_physical": None,
        "raw_value": None,
        "physical_value": None,
        "conversion_status": None,
    }
    agg = telemetry_export._agg_row_to_dict(row)
    assert (agg["bucket"], agg["signal"], agg["capture_session_id"], agg["sample_count"]) == (None, None, None, 0)

    raw = telemetry_export._raw_row_to_dict(row, "both")
    assert (raw["timestamp"], raw["raw_value"], raw["physical_value"]) == (None, None, None)

    buf = io.StringIO()
    writer = csv.writer(buf)
    telemetry_export._write_agg_csv_row(writer, row)
    telemetry_export._write_raw_csv_row(writer, row, "both")
    lines = list(csv.reader(io.StringIO(buf.getvalue())))
    assert lines[0][0] == "" and lines[0][4] == "0"
    assert lines[1][0] == "" and lines[1][2] == ""

    row["bucket"] = row["timestamp"] = datetime(2025, 6, 1, tzinfo=UTC)
    row["capture_session_id"] = uuid.uuid4()
    assert telemetry_export._agg_row_to_dict(row)["bucket"].startswith("2025-06-01")
    assert telemetry_export._raw_row_to_dict(row, "raw")["capture_session_id"] == str(row["capture_session_id"])
