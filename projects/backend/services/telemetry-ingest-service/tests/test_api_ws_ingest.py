"""Tests for WebSocket telemetry ingest endpoint (/api/v1/telemetry/ws)."""
from __future__ import annotations

import hashlib
from uuid import UUID, uuid4

import asyncpg
import pytest


def _token_hash(token: str) -> bytes:
    return hashlib.sha256(token.encode("utf-8")).digest()


async def _seed(
    *,
    db_uri: str,
    project_id: UUID,
    sensor_id: UUID,
    token: str,
    run_id: UUID,
    capture_session_id: UUID,
) -> None:
    conn = await asyncpg.connect(db_uri)
    try:
        await conn.execute(
            "INSERT INTO sensors (id, project_id, status, token_hash) VALUES ($1, $2, 'active', $3)",
            sensor_id,
            project_id,
            _token_hash(token),
        )
        await conn.execute(
            "INSERT INTO runs (id, project_id) VALUES ($1, $2)",
            run_id,
            project_id,
        )
        await conn.execute(
            """
            INSERT INTO capture_sessions (id, run_id, project_id, ordinal_number, status, archived)
            VALUES ($1, $2, $3, 1, 'running', false)
            """,
            capture_session_id,
            run_id,
            project_id,
        )
    finally:
        await conn.close()


# ---------------------------------------------------------------------------
# Happy path
# ---------------------------------------------------------------------------


async def test_ws_ingest_happy_path(service_client, pgsql):
    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()
    token = "ws-test-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    ws = await service_client.ws_connect(
        f"/api/v1/telemetry/ws?sensor_id={sensor_id}&token={token}"
    )
    await ws.send_json(
        {
            "run_id": str(run_id),
            "capture_session_id": str(capture_session_id),
            "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.23}],
        }
    )
    msg = await ws.receive_json()
    assert msg["status"] == "accepted"
    assert msg["accepted"] == 1
    await ws.close()

    conn = await asyncpg.connect(db_uri)
    try:
        count = await conn.fetchval(
            "SELECT COUNT(*) FROM telemetry_records WHERE sensor_id = $1", sensor_id
        )
        assert int(count) == 1
    finally:
        await conn.close()


async def test_ws_ingest_seq_echoed_in_ack(service_client, pgsql):
    project_id, sensor_id, run_id, capture_session_id = uuid4(), uuid4(), uuid4(), uuid4()
    token = "seq-test-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    ws = await service_client.ws_connect(
        f"/api/v1/telemetry/ws?sensor_id={sensor_id}&token={token}"
    )
    await ws.send_json(
        {
            "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 7.0}],
            "seq": 42,
        }
    )
    msg = await ws.receive_json()
    assert msg["status"] == "accepted"
    assert msg["seq"] == 42
    await ws.close()


async def test_ws_ingest_multiple_batches(service_client, pgsql):
    project_id, sensor_id, run_id, capture_session_id = uuid4(), uuid4(), uuid4(), uuid4()
    token = "multi-test-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    ws = await service_client.ws_connect(
        f"/api/v1/telemetry/ws?sensor_id={sensor_id}&token={token}"
    )
    for i in range(3):
        await ws.send_json(
            {
                "run_id": str(run_id),
                "readings": [
                    {"timestamp": f"2026-01-01T00:0{i}:00Z", "raw_value": float(i)},
                    {"timestamp": f"2026-01-01T00:0{i}:01Z", "raw_value": float(i) + 0.5},
                ],
                "seq": i,
            }
        )
        msg = await ws.receive_json()
        assert msg["status"] == "accepted"
        assert msg["accepted"] == 2
        assert msg["seq"] == i

    await ws.close()

    conn = await asyncpg.connect(db_uri)
    try:
        count = await conn.fetchval(
            "SELECT COUNT(*) FROM telemetry_records WHERE sensor_id = $1", sensor_id
        )
        assert int(count) == 6
    finally:
        await conn.close()


# ---------------------------------------------------------------------------
# Authentication / connection errors (tested via plain HTTP GET — the server
# raises HTTP exceptions BEFORE the WebSocket upgrade, so the response is an
# ordinary HTTP response, not a WebSocket error frame)
# ---------------------------------------------------------------------------


async def test_ws_missing_sensor_id_returns_400(service_client):
    resp = await service_client.get("/api/v1/telemetry/ws?token=some-token")
    assert resp.status == 400


async def test_ws_missing_token_returns_401(service_client):
    resp = await service_client.get(f"/api/v1/telemetry/ws?sensor_id={uuid4()}")
    assert resp.status == 401


async def test_ws_invalid_token_returns_401(service_client, pgsql):
    project_id, sensor_id, run_id, capture_session_id = uuid4(), uuid4(), uuid4(), uuid4()
    token = "valid-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    resp = await service_client.get(
        f"/api/v1/telemetry/ws?sensor_id={sensor_id}&token=wrong-token"
    )
    assert resp.status == 401


# ---------------------------------------------------------------------------
# Per-message error handling (connection stays open)
# ---------------------------------------------------------------------------


async def test_ws_invalid_json_returns_error_message(service_client, pgsql):
    project_id, sensor_id, run_id, capture_session_id = uuid4(), uuid4(), uuid4(), uuid4()
    token = "json-err-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    ws = await service_client.ws_connect(
        f"/api/v1/telemetry/ws?sensor_id={sensor_id}&token={token}"
    )

    # Send broken JSON — server should reply with an error but keep connection open.
    await ws.send_str("not valid json {{{")
    err = await ws.receive_json()
    assert err["status"] == "error"
    assert err["code"] == "invalid_json"

    # Connection must still be alive.
    await ws.send_json(
        {"readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0}]}
    )
    ok = await ws.receive_json()
    assert ok["status"] == "accepted"

    await ws.close()


async def test_ws_validation_error_returns_error_message(service_client, pgsql):
    project_id, sensor_id, run_id, capture_session_id = uuid4(), uuid4(), uuid4(), uuid4()
    token = "val-err-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    ws = await service_client.ws_connect(
        f"/api/v1/telemetry/ws?sensor_id={sensor_id}&token={token}"
    )

    # Missing "readings" field.
    await ws.send_json({"run_id": str(run_id)})
    err = await ws.receive_json()
    assert err["status"] == "error"
    assert err["code"] == "validation_error"

    # Connection must still be alive.
    await ws.send_json(
        {"readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 2.0}]}
    )
    ok = await ws.receive_json()
    assert ok["status"] == "accepted"

    await ws.close()


async def test_ws_authorization_header_accepted(service_client, pgsql):
    """Token passed as Authorization: Bearer header (preferred for non-browser clients)."""
    project_id, sensor_id, run_id, capture_session_id = uuid4(), uuid4(), uuid4(), uuid4()
    token = "header-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    ws = await service_client.ws_connect(
        f"/api/v1/telemetry/ws?sensor_id={sensor_id}",
        headers={"Authorization": f"Bearer {token}"},
    )
    await ws.send_json(
        {"readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 5.0}]}
    )
    msg = await ws.receive_json()
    assert msg["status"] == "accepted"
    await ws.close()
