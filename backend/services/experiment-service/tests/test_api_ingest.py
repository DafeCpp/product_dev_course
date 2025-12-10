from __future__ import annotations

# pyright: reportMissingImports=false

import uuid

import asyncpg
import pytest
from aiohttp import WSMsgType

from experiment_service.services.idempotency import IDEMPOTENCY_HEADER
from tests.utils import make_headers


async def _bootstrap_ingest_context(service_client, *, sensor_payload_overrides: dict | None = None):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "Telemetry experiment"},
        headers=headers,
    )
    assert resp.status == 201
    experiment_id = (await resp.json())["id"]

    resp_run = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/runs",
        json={"name": "ingest-run"},
        headers=headers,
    )
    assert resp_run.status == 201
    run = await resp_run.json()
    run_id = run["id"]

    resp_session = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions",
        json={"ordinal_number": 1, "status": "running"},
        headers=headers,
    )
    assert resp_session.status == 201
    capture_session_id = (await resp_session.json())["id"]

    sensor_payload: dict = {
        "project_id": str(project_id),
        "name": "sensor-ingest",
        "type": "thermocouple",
        "input_unit": "mV",
        "display_unit": "C",
    }
    if sensor_payload_overrides:
        sensor_payload.update(sensor_payload_overrides)
    resp_sensor = await service_client.post(
        "/api/v1/sensors",
        json=sensor_payload,
        headers={**headers, IDEMPOTENCY_HEADER: "ingest-sensor"},
    )
    assert resp_sensor.status == 201
    sensor_body = await resp_sensor.json()
    sensor_id = sensor_body["sensor"]["id"]
    token = sensor_body["token"]

    return {
        "project_id": project_id,
        "headers": headers,
        "sensor_id": sensor_id,
        "token": token,
        "run_id": run_id,
        "capture_session_id": capture_session_id,
        "experiment_id": experiment_id,
    }


@pytest.mark.asyncio
async def test_metrics_ingest_accepts_payload(service_client):
    ctx = await _bootstrap_ingest_context(service_client)
    run_id = ctx["run_id"]
    headers = ctx["headers"]
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/metrics",
        json={
            "metrics": [
                {"name": "loss", "step": 1, "value": 0.1, "timestamp": "2025-01-01T00:00:00Z"}
            ]
        },
        headers=headers,
    )
    assert resp.status == 202
    body = await resp.json()
    assert body["accepted"] == 1


@pytest.mark.asyncio
async def test_metrics_query_returns_series(service_client):
    ctx = await _bootstrap_ingest_context(service_client)
    headers = ctx["headers"]
    run_id = ctx["run_id"]
    payload = {
        "metrics": [
            {"name": "loss", "step": 1, "value": 0.1, "timestamp": "2025-01-01T00:00:00Z"}
        ]
    }
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/metrics",
        json=payload,
        headers=headers,
    )
    assert resp.status == 202

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/metrics",
        headers=headers,
    )
    assert resp.status == 200
    data = await resp.json()
    assert data["run_id"] == str(run_id)
    assert data["series"][0]["points"][0]["value"] == 0.1


@pytest.mark.asyncio
async def test_telemetry_stream_stub_closes_with_error(service_client):
    ws = await service_client.ws_connect("/api/v1/telemetry/stream")
    message = await ws.receive()
    assert message.type == WSMsgType.CLOSE
    assert ws.close_code == 1011
    assert message.extra == "Streaming not implemented"


@pytest.mark.asyncio
async def test_telemetry_ingest_accepts_payload(service_client):
    ctx = await _bootstrap_ingest_context(service_client)
    payload = {
        "sensor_id": ctx["sensor_id"],
        "run_id": ctx["run_id"],
        "capture_session_id": ctx["capture_session_id"],
        "readings": [
            {"timestamp": "2025-01-01T00:00:00Z", "raw_value": 1.23},
        ],
    }
    resp = await service_client.post(
        "/api/v1/telemetry",
        json=payload,
        headers={"Authorization": f"Bearer {ctx['token']}"},
    )
    assert resp.status == 202
    body = await resp.json()
    assert body["status"] == "accepted"
    assert body["accepted"] == 1


@pytest.mark.asyncio
async def test_telemetry_ingest_requires_token(service_client):
    ctx = await _bootstrap_ingest_context(service_client)
    payload = {
        "sensor_id": ctx["sensor_id"],
        "readings": [{"timestamp": "2025-01-01T00:00:00Z", "raw_value": 1.0}],
    }
    resp = await service_client.post("/api/v1/telemetry", json=payload)
    assert resp.status == 401


@pytest.mark.asyncio
async def test_telemetry_ingest_invalid_token(service_client):
    ctx = await _bootstrap_ingest_context(service_client)
    payload = {
        "sensor_id": ctx["sensor_id"],
        "readings": [{"timestamp": "2025-01-01T00:00:00Z", "raw_value": 1.0}],
    }
    resp = await service_client.post(
        "/api/v1/telemetry",
        json=payload,
        headers={"Authorization": "Bearer invalid-token"},
    )
    assert resp.status == 401


@pytest.mark.asyncio
async def test_telemetry_ingest_rejects_mismatched_capture(service_client):
    ctx = await _bootstrap_ingest_context(service_client)
    # Create another run to produce mismatch
    headers = ctx["headers"]
    resp_run = await service_client.post(
        f"/api/v1/experiments/{ctx['experiment_id']}/runs",
        json={"name": "ingest-run-2"},
        headers=headers,
    )
    assert resp_run.status == 201
    other_run_id = (await resp_run.json())["id"]

    payload = {
        "sensor_id": ctx["sensor_id"],
        "run_id": other_run_id,
        "capture_session_id": ctx["capture_session_id"],
        "readings": [{"timestamp": "2025-01-01T00:00:00Z", "raw_value": 2.0}],
    }
    resp = await service_client.post(
        "/api/v1/telemetry",
        json=payload,
        headers={"Authorization": f"Bearer {ctx['token']}"},
    )
    assert resp.status == 400


@pytest.mark.asyncio
async def test_telemetry_ingest_requires_readings(service_client):
    ctx = await _bootstrap_ingest_context(service_client)
    payload = {"sensor_id": ctx["sensor_id"], "readings": []}
    resp = await service_client.post(
        "/api/v1/telemetry",
        json=payload,
        headers={"Authorization": f"Bearer {ctx['token']}"},
    )
    assert resp.status == 400


@pytest.mark.asyncio
async def test_telemetry_ingest_persists_records(service_client, pgsql):
    ctx = await _bootstrap_ingest_context(
        service_client,
        sensor_payload_overrides={
            "conversion_profile": {
                "version": "v1",
                "kind": "linear",
                "payload": {"a": 2.0, "b": 1.0},
                "status": "active",
            }
        },
    )
    payload = {
        "sensor_id": ctx["sensor_id"],
        "run_id": ctx["run_id"],
        "capture_session_id": ctx["capture_session_id"],
        "readings": [
            {"timestamp": "2025-01-01T00:00:00Z", "raw_value": 1.5},
            {"timestamp": "2025-01-01T00:00:05Z", "raw_value": 2.0, "physical_value": 42.0},
        ],
    }
    resp = await service_client.post(
        "/api/v1/telemetry",
        json=payload,
        headers={"Authorization": f"Bearer {ctx['token']}"},
    )
    assert resp.status == 202
    body = await resp.json()
    assert body["accepted"] == len(payload["readings"])

    conninfo = pgsql["experiment_service"].conninfo
    conn = await asyncpg.connect(dsn=conninfo.get_uri())
    try:
        rows = await conn.fetch(
            """
            SELECT timestamp,
                   raw_value,
                   physical_value,
                   conversion_status,
                   conversion_profile_id
            FROM telemetry_records
            WHERE sensor_id = $1
            ORDER BY timestamp
            """,
            uuid.UUID(ctx["sensor_id"]),
        )
        assert len(rows) == 2
        first = rows[0]
        second = rows[1]
        assert first["conversion_status"] == "converted"
        assert first["physical_value"] == pytest.approx(2 * 1.5 + 1)
        assert first["conversion_profile_id"] is not None
        assert second["conversion_status"] == "client_provided"
        assert second["physical_value"] == 42.0

        sensor_row = await conn.fetchrow(
            "SELECT last_heartbeat, status FROM sensors WHERE id = $1",
            uuid.UUID(ctx["sensor_id"]),
        )
        assert sensor_row is not None
        assert sensor_row["status"] == "active"
        assert sensor_row["last_heartbeat"] == second["timestamp"]
    finally:
        await conn.close()

