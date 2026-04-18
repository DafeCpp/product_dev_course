"""Integration tests for telemetry export endpoints.

Scenario: create session → ingest data (direct SQL) → export → verify CSV/JSON.

Covered:
  - GET /api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export
  - GET /api/v1/runs/{run_id}/telemetry/export
  - format=csv|json
  - raw_or_physical=raw|physical|both
  - include_late=true|false
  - sensor_id filter
  - signal filter
  - capture_session_id filter (run-level export)
  - empty capture session
  - run with no capture sessions
  - RBAC: viewer has access, unauthorized has not
  - invalid format / invalid UUID
  - streaming: all rows returned, no X-Export-Truncated header
"""
from __future__ import annotations

import csv
import io
import json
import uuid
from datetime import datetime, timezone

import asyncpg
import pytest

from experiment_service.services.idempotency import IDEMPOTENCY_HEADER
from tests.utils import make_headers


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

async def _setup_context(service_client, *, project_id: uuid.UUID | None = None):
    """Create experiment → run → capture session (running) → sensor.

    Returns dict with keys:
        project_id, headers, experiment_id, run_id,
        capture_session_id, sensor_id, sensor_uuid
    """
    if project_id is None:
        project_id = uuid.uuid4()
    headers = make_headers(project_id)

    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": f"TelEx {uuid.uuid4()}"},
        headers=headers,
    )
    assert resp.status == 201
    experiment_id = (await resp.json())["id"]

    resp = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/runs",
        json={"name": "telemetry-export-run"},
        headers=headers,
    )
    assert resp.status == 201
    run_id = (await resp.json())["id"]

    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions",
        json={"ordinal_number": 1, "status": "running"},
        headers=headers,
    )
    assert resp.status == 201
    capture_session_id = (await resp.json())["id"]

    resp = await service_client.post(
        "/api/v1/sensors",
        json={
            "project_id": str(project_id),
            "name": f"sensor-{uuid.uuid4()}",
            "type": "thermocouple",
            "input_unit": "mV",
            "display_unit": "C",
        },
        headers={**headers, IDEMPOTENCY_HEADER: str(uuid.uuid4())},
    )
    assert resp.status == 201
    sensor_body = await resp.json()
    sensor_id = sensor_body["sensor"]["id"]

    return {
        "project_id": project_id,
        "headers": headers,
        "experiment_id": experiment_id,
        "run_id": run_id,
        "capture_session_id": capture_session_id,
        "sensor_id": sensor_id,
        "sensor_uuid": uuid.UUID(sensor_id),
    }


async def _insert_telemetry(
    pgsql,
    *,
    project_id: uuid.UUID,
    sensor_id: uuid.UUID,
    run_id: uuid.UUID,
    capture_session_id: uuid.UUID,
    records: list[dict],
):
    """Insert telemetry_records rows directly via asyncpg.

    Each record dict may contain:
        timestamp (datetime, required),
        raw_value (float, required),
        physical_value (float | None),
        signal (str, default "temp"),
        conversion_status (str, default "raw_only"),
        late (bool, default False),
    """
    conninfo = pgsql["experiment_service"].conninfo
    conn = await asyncpg.connect(dsn=conninfo.get_uri())
    try:
        for rec in records:
            signal = rec.get("signal", "temp")
            meta: dict = {"signal": signal}
            if rec.get("late"):
                meta["__system"] = {"late": True}
            await conn.execute(
                """
                INSERT INTO telemetry_records
                    (project_id, sensor_id, run_id, capture_session_id,
                     timestamp, raw_value, physical_value, meta, conversion_status)
                VALUES ($1, $2, $3, $4, $5, $6, $7, $8::jsonb,
                        $9::telemetry_conversion_status)
                """,
                project_id,
                sensor_id,
                run_id,
                capture_session_id,
                rec["timestamp"],
                float(rec["raw_value"]),
                float(rec["physical_value"]) if rec.get("physical_value") is not None else None,
                json.dumps(meta),
                rec.get("conversion_status", "raw_only"),
            )
    finally:
        await conn.close()


def _ts(offset_seconds: int = 0) -> datetime:
    """Return a fixed UTC datetime shifted by offset_seconds."""
    base = datetime(2025, 6, 1, 12, 0, 0, tzinfo=timezone.utc)
    from datetime import timedelta
    return base + timedelta(seconds=offset_seconds)


# ---------------------------------------------------------------------------
# Session export — CSV
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_export_session_csv_basic(service_client, pgsql):
    """Create session, insert 3 records, export CSV — verify headers and data rows."""
    ctx = await _setup_context(service_client)
    project_id = ctx["project_id"]
    run_id = ctx["run_id"]
    session_id = ctx["capture_session_id"]
    sensor_uuid = ctx["sensor_uuid"]
    headers = ctx["headers"]

    await _insert_telemetry(
        pgsql,
        project_id=project_id,
        sensor_id=sensor_uuid,
        run_id=uuid.UUID(run_id),
        capture_session_id=uuid.UUID(session_id),
        records=[
            {"timestamp": _ts(0), "raw_value": 1.0, "physical_value": 10.0, "conversion_status": "converted"},
            {"timestamp": _ts(1), "raw_value": 2.0, "physical_value": 20.0, "conversion_status": "converted"},
            {"timestamp": _ts(2), "raw_value": 3.0, "physical_value": 30.0, "conversion_status": "converted"},
        ],
    )

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export"
        f"?format=csv&project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 200
    assert "text/csv" in resp.headers.get("Content-Type", "")
    assert "attachment" in resp.headers.get("Content-Disposition", "")
    assert f"telemetry_{session_id}" in resp.headers.get("Content-Disposition", "")

    body = await resp.text()
    rows = list(csv.reader(io.StringIO(body)))
    assert len(rows) == 4  # header + 3 data rows
    header = rows[0]
    assert "timestamp" in header
    assert "sensor_id" in header
    assert "signal" in header
    assert "raw_value" in header
    assert "physical_value" in header
    assert "conversion_status" in header
    assert "capture_session_id" in header

    # All data rows reference the correct session
    cs_idx = header.index("capture_session_id")
    for row in rows[1:]:
        assert row[cs_idx] == session_id


@pytest.mark.asyncio
async def test_export_session_csv_values_correct(service_client, pgsql):
    """Verify actual numeric values in CSV output."""
    ctx = await _setup_context(service_client)
    project_id = ctx["project_id"]
    run_id = ctx["run_id"]
    session_id = ctx["capture_session_id"]
    sensor_uuid = ctx["sensor_uuid"]
    headers = ctx["headers"]

    await _insert_telemetry(
        pgsql,
        project_id=project_id,
        sensor_id=sensor_uuid,
        run_id=uuid.UUID(run_id),
        capture_session_id=uuid.UUID(session_id),
        records=[
            {"timestamp": _ts(0), "raw_value": 42.5, "physical_value": 100.0, "conversion_status": "converted"},
        ],
    )

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export"
        f"?format=csv&project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 200
    body = await resp.text()
    rows = list(csv.reader(io.StringIO(body)))
    header = rows[0]

    raw_idx = header.index("raw_value")
    phys_idx = header.index("physical_value")
    status_idx = header.index("conversion_status")
    sensor_idx = header.index("sensor_id")

    data = rows[1]
    assert float(data[raw_idx]) == pytest.approx(42.5)
    assert float(data[phys_idx]) == pytest.approx(100.0)
    assert data[status_idx] == "converted"
    assert data[sensor_idx] == ctx["sensor_id"]


# ---------------------------------------------------------------------------
# Session export — JSON
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_export_session_json_basic(service_client, pgsql):
    """Export session telemetry as JSON — verify structure and content."""
    ctx = await _setup_context(service_client)
    project_id = ctx["project_id"]
    run_id = ctx["run_id"]
    session_id = ctx["capture_session_id"]
    sensor_uuid = ctx["sensor_uuid"]
    headers = ctx["headers"]

    await _insert_telemetry(
        pgsql,
        project_id=project_id,
        sensor_id=sensor_uuid,
        run_id=uuid.UUID(run_id),
        capture_session_id=uuid.UUID(session_id),
        records=[
            {"timestamp": _ts(0), "raw_value": 5.0, "physical_value": 50.0, "conversion_status": "converted"},
            {"timestamp": _ts(1), "raw_value": 6.0, "physical_value": 60.0, "conversion_status": "converted"},
        ],
    )

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export"
        f"?format=json&project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 200
    assert "application/json" in resp.headers.get("Content-Type", "")

    data = json.loads(await resp.text())
    assert isinstance(data, list)
    assert len(data) == 2
    item = data[0]
    assert "timestamp" in item
    assert "sensor_id" in item
    assert "raw_value" in item
    assert "physical_value" in item
    assert "conversion_status" in item
    assert "capture_session_id" in item
    assert item["sensor_id"] == ctx["sensor_id"]
    assert item["capture_session_id"] == session_id


# ---------------------------------------------------------------------------
# raw_or_physical filter
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_export_session_raw_only_columns(service_client, pgsql):
    """raw_or_physical=raw — CSV must not contain physical_value column."""
    ctx = await _setup_context(service_client)
    project_id = ctx["project_id"]
    run_id = ctx["run_id"]
    session_id = ctx["capture_session_id"]
    sensor_uuid = ctx["sensor_uuid"]
    headers = ctx["headers"]

    await _insert_telemetry(
        pgsql,
        project_id=project_id,
        sensor_id=sensor_uuid,
        run_id=uuid.UUID(run_id),
        capture_session_id=uuid.UUID(session_id),
        records=[{"timestamp": _ts(0), "raw_value": 1.0}],
    )

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export"
        f"?format=csv&raw_or_physical=raw&project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 200
    header = list(csv.reader(io.StringIO(await resp.text())))[0]
    assert "raw_value" in header
    assert "physical_value" not in header


@pytest.mark.asyncio
async def test_export_session_physical_only_columns(service_client, pgsql):
    """raw_or_physical=physical — CSV must not contain raw_value column."""
    ctx = await _setup_context(service_client)
    project_id = ctx["project_id"]
    run_id = ctx["run_id"]
    session_id = ctx["capture_session_id"]
    sensor_uuid = ctx["sensor_uuid"]
    headers = ctx["headers"]

    await _insert_telemetry(
        pgsql,
        project_id=project_id,
        sensor_id=sensor_uuid,
        run_id=uuid.UUID(run_id),
        capture_session_id=uuid.UUID(session_id),
        records=[{"timestamp": _ts(0), "raw_value": 1.0, "physical_value": 10.0}],
    )

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export"
        f"?format=csv&raw_or_physical=physical&project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 200
    header = list(csv.reader(io.StringIO(await resp.text())))[0]
    assert "physical_value" in header
    assert "raw_value" not in header


# ---------------------------------------------------------------------------
# include_late filter
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_export_session_include_late_default_true(service_client, pgsql):
    """By default late records are included."""
    ctx = await _setup_context(service_client)
    project_id = ctx["project_id"]
    run_id = ctx["run_id"]
    session_id = ctx["capture_session_id"]
    sensor_uuid = ctx["sensor_uuid"]
    headers = ctx["headers"]

    await _insert_telemetry(
        pgsql,
        project_id=project_id,
        sensor_id=sensor_uuid,
        run_id=uuid.UUID(run_id),
        capture_session_id=uuid.UUID(session_id),
        records=[
            {"timestamp": _ts(0), "raw_value": 1.0},
            {"timestamp": _ts(1), "raw_value": 2.0, "late": True},
        ],
    )

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export"
        f"?format=json&project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 200
    data = json.loads(await resp.text())
    assert len(data) == 2


@pytest.mark.asyncio
async def test_export_session_exclude_late(service_client, pgsql):
    """include_late=false must exclude records with meta.__system.late=true."""
    ctx = await _setup_context(service_client)
    project_id = ctx["project_id"]
    run_id = ctx["run_id"]
    session_id = ctx["capture_session_id"]
    sensor_uuid = ctx["sensor_uuid"]
    headers = ctx["headers"]

    await _insert_telemetry(
        pgsql,
        project_id=project_id,
        sensor_id=sensor_uuid,
        run_id=uuid.UUID(run_id),
        capture_session_id=uuid.UUID(session_id),
        records=[
            {"timestamp": _ts(0), "raw_value": 1.0},
            {"timestamp": _ts(1), "raw_value": 2.0, "late": True},
            {"timestamp": _ts(2), "raw_value": 3.0},
        ],
    )

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export"
        f"?format=json&include_late=false&project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 200
    data = json.loads(await resp.text())
    assert len(data) == 2
    raw_values = {item["raw_value"] for item in data}
    assert 2.0 not in raw_values  # late record excluded


# ---------------------------------------------------------------------------
# sensor_id filter
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_export_session_filter_by_sensor(service_client, pgsql):
    """sensor_id filter returns only records from that sensor."""
    ctx = await _setup_context(service_client)
    project_id = ctx["project_id"]
    run_id = ctx["run_id"]
    session_id = ctx["capture_session_id"]
    sensor_uuid = ctx["sensor_uuid"]
    headers = ctx["headers"]

    # Create a second sensor in the same project
    resp = await service_client.post(
        "/api/v1/sensors",
        json={
            "project_id": str(project_id),
            "name": f"sensor2-{uuid.uuid4()}",
            "type": "accelerometer",
            "input_unit": "g",
            "display_unit": "m/s2",
        },
        headers={**headers, IDEMPOTENCY_HEADER: str(uuid.uuid4())},
    )
    assert resp.status == 201
    sensor2_id = uuid.UUID((await resp.json())["sensor"]["id"])

    await _insert_telemetry(
        pgsql,
        project_id=project_id,
        sensor_id=sensor_uuid,
        run_id=uuid.UUID(run_id),
        capture_session_id=uuid.UUID(session_id),
        records=[{"timestamp": _ts(0), "raw_value": 1.0, "signal": "temp"}],
    )
    await _insert_telemetry(
        pgsql,
        project_id=project_id,
        sensor_id=sensor2_id,
        run_id=uuid.UUID(run_id),
        capture_session_id=uuid.UUID(session_id),
        records=[{"timestamp": _ts(1), "raw_value": 9.0, "signal": "accel_x"}],
    )

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export"
        f"?format=json&sensor_id={sensor_uuid}&project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 200
    data = json.loads(await resp.text())
    assert len(data) == 1
    assert data[0]["sensor_id"] == str(sensor_uuid)


# ---------------------------------------------------------------------------
# signal filter
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_export_session_filter_by_signal(service_client, pgsql):
    """signal filter returns only records with matching signal name."""
    ctx = await _setup_context(service_client)
    project_id = ctx["project_id"]
    run_id = ctx["run_id"]
    session_id = ctx["capture_session_id"]
    sensor_uuid = ctx["sensor_uuid"]
    headers = ctx["headers"]

    await _insert_telemetry(
        pgsql,
        project_id=project_id,
        sensor_id=sensor_uuid,
        run_id=uuid.UUID(run_id),
        capture_session_id=uuid.UUID(session_id),
        records=[
            {"timestamp": _ts(0), "raw_value": 1.0, "signal": "temperature"},
            {"timestamp": _ts(1), "raw_value": 2.0, "signal": "humidity"},
            {"timestamp": _ts(2), "raw_value": 3.0, "signal": "temperature"},
        ],
    )

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export"
        f"?format=json&signal=temperature&project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 200
    data = json.loads(await resp.text())
    assert len(data) == 2
    assert all(item["signal"] == "temperature" for item in data)


# ---------------------------------------------------------------------------
# Empty states
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_export_session_empty_csv(service_client, pgsql):
    """Empty session — CSV export returns header-only."""
    ctx = await _setup_context(service_client)
    project_id = ctx["project_id"]
    run_id = ctx["run_id"]
    session_id = ctx["capture_session_id"]
    headers = ctx["headers"]

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export"
        f"?format=csv&project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 200
    body = await resp.text()
    rows = list(csv.reader(io.StringIO(body)))
    assert len(rows) == 1  # header only
    assert "timestamp" in rows[0]


@pytest.mark.asyncio
async def test_export_session_empty_json(service_client, pgsql):
    """Empty session — JSON export returns empty array."""
    ctx = await _setup_context(service_client)
    project_id = ctx["project_id"]
    run_id = ctx["run_id"]
    session_id = ctx["capture_session_id"]
    headers = ctx["headers"]

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export"
        f"?format=json&project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 200
    data = json.loads(await resp.text())
    assert data == []


# ---------------------------------------------------------------------------
# Run-level export
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_export_run_csv_combines_sessions(service_client, pgsql):
    """Run export aggregates data from all capture sessions."""
    ctx = await _setup_context(service_client)
    project_id = ctx["project_id"]
    run_id = ctx["run_id"]
    session1_id = ctx["capture_session_id"]
    sensor_uuid = ctx["sensor_uuid"]
    headers = ctx["headers"]

    # Stop first session and create second
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions/{session1_id}/stop",
        json={"status": "succeeded"},
        headers=headers,
    )
    assert resp.status == 200

    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions",
        json={"ordinal_number": 2, "status": "running"},
        headers=headers,
    )
    assert resp.status == 201
    session2_id = (await resp.json())["id"]

    await _insert_telemetry(
        pgsql,
        project_id=project_id,
        sensor_id=sensor_uuid,
        run_id=uuid.UUID(run_id),
        capture_session_id=uuid.UUID(session1_id),
        records=[{"timestamp": _ts(0), "raw_value": 1.0}],
    )
    await _insert_telemetry(
        pgsql,
        project_id=project_id,
        sensor_id=sensor_uuid,
        run_id=uuid.UUID(run_id),
        capture_session_id=uuid.UUID(session2_id),
        records=[{"timestamp": _ts(1), "raw_value": 2.0}],
    )

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/telemetry/export?format=json&project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 200
    assert "attachment" in resp.headers.get("Content-Disposition", "")
    assert f"telemetry_run_{run_id}" in resp.headers.get("Content-Disposition", "")

    data = json.loads(await resp.text())
    assert len(data) == 2
    session_ids = {item["capture_session_id"] for item in data}
    assert session1_id in session_ids
    assert session2_id in session_ids


@pytest.mark.asyncio
async def test_export_run_no_sessions(service_client, pgsql):
    """Run with no capture sessions returns empty result."""
    ctx = await _setup_context(service_client)
    project_id = ctx["project_id"]
    headers = ctx["headers"]

    # Create a separate run without any capture sessions
    resp = await service_client.post(
        f"/api/v1/experiments/{ctx['experiment_id']}/runs",
        json={"name": "empty-run"},
        headers=headers,
    )
    assert resp.status == 201
    empty_run_id = (await resp.json())["id"]

    resp = await service_client.get(
        f"/api/v1/runs/{empty_run_id}/telemetry/export?format=json&project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 200
    data = json.loads(await resp.text())
    assert data == []


@pytest.mark.asyncio
async def test_export_run_filter_by_capture_session(service_client, pgsql):
    """capture_session_id filter on run export limits to that session only."""
    ctx = await _setup_context(service_client)
    project_id = ctx["project_id"]
    run_id = ctx["run_id"]
    session1_id = ctx["capture_session_id"]
    sensor_uuid = ctx["sensor_uuid"]
    headers = ctx["headers"]

    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions/{session1_id}/stop",
        json={"status": "succeeded"},
        headers=headers,
    )
    assert resp.status == 200

    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions",
        json={"ordinal_number": 2, "status": "running"},
        headers=headers,
    )
    assert resp.status == 201
    session2_id = (await resp.json())["id"]

    await _insert_telemetry(
        pgsql,
        project_id=project_id,
        sensor_id=sensor_uuid,
        run_id=uuid.UUID(run_id),
        capture_session_id=uuid.UUID(session1_id),
        records=[{"timestamp": _ts(0), "raw_value": 1.0}],
    )
    await _insert_telemetry(
        pgsql,
        project_id=project_id,
        sensor_id=sensor_uuid,
        run_id=uuid.UUID(run_id),
        capture_session_id=uuid.UUID(session2_id),
        records=[{"timestamp": _ts(1), "raw_value": 99.0}],
    )

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/telemetry/export"
        f"?format=json&capture_session_id={session1_id}&project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 200
    data = json.loads(await resp.text())
    assert len(data) == 1
    assert data[0]["capture_session_id"] == session1_id


# ---------------------------------------------------------------------------
# RBAC
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_export_session_viewer_can_read(service_client, pgsql):
    """Viewer role should be able to export telemetry (read-only access)."""
    ctx = await _setup_context(service_client)
    project_id = ctx["project_id"]
    run_id = ctx["run_id"]
    session_id = ctx["capture_session_id"]
    headers = ctx["headers"]
    sensor_uuid = ctx["sensor_uuid"]

    await _insert_telemetry(
        pgsql,
        project_id=project_id,
        sensor_id=sensor_uuid,
        run_id=uuid.UUID(run_id),
        capture_session_id=uuid.UUID(session_id),
        records=[{"timestamp": _ts(0), "raw_value": 1.0}],
    )

    viewer_headers = make_headers(project_id, role="viewer")
    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export"
        f"?format=json&project_id={project_id}",
        headers=viewer_headers,
    )
    assert resp.status == 200


@pytest.mark.asyncio
async def test_export_session_other_project_forbidden(service_client, pgsql):
    """Request with a different project_id in header is rejected (403)."""
    ctx = await _setup_context(service_client)
    run_id = ctx["run_id"]
    session_id = ctx["capture_session_id"]

    other_project_id = uuid.uuid4()
    other_headers = make_headers(other_project_id)

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export"
        f"?format=json&project_id={other_project_id}",
        headers=other_headers,
    )
    # Run belongs to original project — service should return 404 (not found in that project)
    assert resp.status in (403, 404)


# ---------------------------------------------------------------------------
# Invalid params
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_export_session_invalid_format(service_client, pgsql):
    """format=xml should return 400."""
    ctx = await _setup_context(service_client)
    project_id = ctx["project_id"]
    run_id = ctx["run_id"]
    session_id = ctx["capture_session_id"]
    headers = ctx["headers"]

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export"
        f"?format=xml&project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 400


@pytest.mark.asyncio
async def test_export_session_invalid_raw_or_physical(service_client, pgsql):
    """raw_or_physical=all should return 400."""
    ctx = await _setup_context(service_client)
    project_id = ctx["project_id"]
    run_id = ctx["run_id"]
    session_id = ctx["capture_session_id"]
    headers = ctx["headers"]

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export"
        f"?raw_or_physical=all&project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 400


@pytest.mark.asyncio
async def test_export_session_not_found(service_client, pgsql):
    """Non-existent session returns 404."""
    ctx = await _setup_context(service_client)
    project_id = ctx["project_id"]
    run_id = ctx["run_id"]
    headers = ctx["headers"]
    fake_session_id = uuid.uuid4()

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{fake_session_id}/telemetry/export"
        f"?format=json&project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 404


@pytest.mark.asyncio
async def test_export_run_not_found(service_client, pgsql):
    """Non-existent run returns 404."""
    ctx = await _setup_context(service_client)
    project_id = ctx["project_id"]
    headers = ctx["headers"]
    fake_run_id = uuid.uuid4()

    resp = await service_client.get(
        f"/api/v1/runs/{fake_run_id}/telemetry/export"
        f"?format=json&project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 404


# ---------------------------------------------------------------------------
# Streaming — no truncation
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_export_all_rows_returned_no_truncation(service_client, pgsql):
    """Streaming export returns all rows without any row limit or truncation header."""
    ctx = await _setup_context(service_client)
    project_id = ctx["project_id"]
    run_id = ctx["run_id"]
    session_id = ctx["capture_session_id"]
    sensor_uuid = ctx["sensor_uuid"]
    headers = ctx["headers"]

    row_count = 50
    await _insert_telemetry(
        pgsql,
        project_id=project_id,
        sensor_id=sensor_uuid,
        run_id=uuid.UUID(run_id),
        capture_session_id=uuid.UUID(session_id),
        records=[
            {"timestamp": _ts(i), "raw_value": float(i), "conversion_status": "raw_only"}
            for i in range(row_count)
        ],
    )

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export"
        f"?format=json&project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 200
    assert "X-Export-Truncated" not in resp.headers
    data = json.loads(await resp.text())
    assert len(data) == row_count


@pytest.mark.asyncio
async def test_export_not_truncated_header_absent(service_client, pgsql):
    """When results fit within limit, X-Export-Truncated header must not be set."""
    ctx = await _setup_context(service_client)
    project_id = ctx["project_id"]
    run_id = ctx["run_id"]
    session_id = ctx["capture_session_id"]
    sensor_uuid = ctx["sensor_uuid"]
    headers = ctx["headers"]

    await _insert_telemetry(
        pgsql,
        project_id=project_id,
        sensor_id=sensor_uuid,
        run_id=uuid.UUID(run_id),
        capture_session_id=uuid.UUID(session_id),
        records=[{"timestamp": _ts(0), "raw_value": 1.0}],
    )

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export"
        f"?format=json&project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 200
    assert "X-Export-Truncated" not in resp.headers


# ---------------------------------------------------------------------------
# Ordering
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_export_session_ordered_by_timestamp_asc(service_client, pgsql):
    """Exported records must be ordered by timestamp ASC."""
    ctx = await _setup_context(service_client)
    project_id = ctx["project_id"]
    run_id = ctx["run_id"]
    session_id = ctx["capture_session_id"]
    sensor_uuid = ctx["sensor_uuid"]
    headers = ctx["headers"]

    # Insert in reverse order
    await _insert_telemetry(
        pgsql,
        project_id=project_id,
        sensor_id=sensor_uuid,
        run_id=uuid.UUID(run_id),
        capture_session_id=uuid.UUID(session_id),
        records=[
            {"timestamp": _ts(10), "raw_value": 3.0},
            {"timestamp": _ts(0), "raw_value": 1.0},
            {"timestamp": _ts(5), "raw_value": 2.0},
        ],
    )

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export"
        f"?format=json&raw_or_physical=raw&project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 200
    data = json.loads(await resp.text())
    assert len(data) == 3
    raw_values = [item["raw_value"] for item in data]
    assert raw_values == sorted(raw_values), "Records must be in ascending timestamp order"


# ---------------------------------------------------------------------------
# Default format
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_export_session_default_format_is_csv(service_client, pgsql):
    """Default format (no format param) must be CSV."""
    ctx = await _setup_context(service_client)
    project_id = ctx["project_id"]
    run_id = ctx["run_id"]
    session_id = ctx["capture_session_id"]
    headers = ctx["headers"]

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export"
        f"?project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 200
    assert "text/csv" in resp.headers.get("Content-Type", "")
