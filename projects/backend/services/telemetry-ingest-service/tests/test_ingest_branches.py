"""Branch coverage for REST/WS ingest error paths and ingest-time meta/conversion handling.

Runs against the real PostgreSQL/TimescaleDB instance.
"""

from __future__ import annotations

import hashlib
import json
from uuid import UUID, uuid4

import aiohttp
import asyncpg

from telemetry_ingest_service.services.telemetry import TelemetryIngestService


def _token_hash(token: str) -> bytes:
    return hashlib.sha256(token.encode("utf-8")).digest()


def _db_uri(pgsql) -> str:
    return pgsql["telemetry_ingest_service"].conninfo.get_uri()


async def _seed(db_uri: str, *, token: str = "sensor-token") -> dict[str, UUID]:
    ids = {
        "project_id": uuid4(),
        "sensor_id": uuid4(),
        "run_id": uuid4(),
        "capture_session_id": uuid4(),
    }
    conn = await asyncpg.connect(db_uri)
    try:
        await conn.execute(
            "INSERT INTO sensors (id, project_id, status, token_hash) VALUES ($1, $2, 'active', $3)",
            ids["sensor_id"],
            ids["project_id"],
            _token_hash(token),
        )
        await conn.execute(
            "INSERT INTO runs (id, project_id) VALUES ($1, $2)", ids["run_id"], ids["project_id"]
        )
        await conn.execute(
            """
            INSERT INTO capture_sessions (id, run_id, project_id, ordinal_number, status, archived)
            VALUES ($1, $2, $3, 1, 'running', false)
            """,
            ids["capture_session_id"],
            ids["run_id"],
            ids["project_id"],
        )
    finally:
        await conn.close()
    return ids


async def _activate_profile(db_uri: str, ids: dict[str, UUID], *, kind: str, payload: dict) -> UUID:
    conn = await asyncpg.connect(db_uri)
    try:
        profile_id = await conn.fetchval(
            """
            INSERT INTO conversion_profiles (sensor_id, project_id, kind, payload, status)
            VALUES ($1, $2, $3, $4::jsonb, 'active') RETURNING id
            """,
            ids["sensor_id"],
            ids["project_id"],
            kind,
            json.dumps(payload),
        )
        await conn.execute(
            "UPDATE sensors SET active_profile_id = $1 WHERE id = $2", profile_id, ids["sensor_id"]
        )
    finally:
        await conn.close()
    return profile_id


async def _records(db_uri: str, sensor_id: UUID) -> list[asyncpg.Record]:
    conn = await asyncpg.connect(db_uri)
    try:
        return await conn.fetch(
            """
            SELECT raw_value, physical_value, conversion_status, conversion_profile_id, meta
            FROM telemetry_records WHERE sensor_id = $1 ORDER BY id
            """,
            sensor_id,
        )
    finally:
        await conn.close()


def _auth(token: str = "sensor-token") -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


# ---------------------------------------------------------------------------
# REST ingest error paths
# ---------------------------------------------------------------------------


async def test_ingest_invalid_payload_400(service_client):
    resp = await service_client.post(
        "/api/v1/telemetry", json={"sensor_id": "nope"}, headers=_auth()
    )
    assert resp.status == 400


async def test_ingest_unknown_run_404_and_logged(service_client, pgsql):
    db_uri = _db_uri(pgsql)
    ids = await _seed(db_uri)
    resp = await service_client.post(
        "/api/v1/telemetry",
        json={
            "sensor_id": str(ids["sensor_id"]),
            "run_id": str(uuid4()),
            "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0}],
        },
        headers=_auth(),
    )
    assert resp.status == 404
    assert await _records(db_uri, ids["sensor_id"]) == []


# ---------------------------------------------------------------------------
# Ingest service: meta merge and conversion branches
# ---------------------------------------------------------------------------


async def test_ingest_merges_client_system_meta_with_auto_attach_marker(service_client, pgsql):
    db_uri = _db_uri(pgsql)
    ids = await _seed(db_uri)
    resp = await service_client.post(
        "/api/v1/telemetry",
        json={
            "sensor_id": str(ids["sensor_id"]),
            "run_id": str(ids["run_id"]),
            "meta": {"__system": {"client_flag": 1}, "device": "d1"},
            "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0}],
        },
        headers=_auth(),
    )
    assert resp.status == 202
    [row] = await _records(db_uri, ids["sensor_id"])
    meta = json.loads(row["meta"])
    assert meta["device"] == "d1"
    assert meta["__system"] == {"client_flag": 1, "capture_session_auto_attached": True}


async def test_ingest_marks_conversion_failed_when_profile_raises(service_client, pgsql):
    db_uri = _db_uri(pgsql)
    ids = await _seed(db_uri)
    # Lookup table points without "physical" make the converter raise on the clamp path.
    profile_id = await _activate_profile(
        db_uri,
        ids,
        kind="lookup_table",
        payload={"table": [{"raw": 0}, {"raw": 10}]},
    )
    resp = await service_client.post(
        "/api/v1/telemetry",
        json={
            "sensor_id": str(ids["sensor_id"]),
            "capture_session_id": str(ids["capture_session_id"]),
            "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": -5.0}],
        },
        headers=_auth(),
    )
    assert resp.status == 202
    [row] = await _records(db_uri, ids["sensor_id"])
    assert row["conversion_status"] == "conversion_failed"
    assert row["physical_value"] is None
    assert row["conversion_profile_id"] == profile_id


async def test_ingest_keeps_client_physical_value_but_links_active_profile(service_client, pgsql):
    db_uri = _db_uri(pgsql)
    ids = await _seed(db_uri)
    profile_id = await _activate_profile(db_uri, ids, kind="linear", payload={"a": 2, "b": 0})
    resp = await service_client.post(
        "/api/v1/telemetry",
        json={
            "sensor_id": str(ids["sensor_id"]),
            "capture_session_id": str(ids["capture_session_id"]),
            "readings": [
                {"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0, "physical_value": 42.0},
                {"timestamp": "2026-01-01T00:00:01Z", "raw_value": 3.0},
            ],
        },
        headers=_auth(),
    )
    assert resp.status == 202
    client_row, converted_row = await _records(db_uri, ids["sensor_id"])
    assert (client_row["physical_value"], client_row["conversion_status"]) == (
        42.0,
        "client_provided",
    )
    assert client_row["conversion_profile_id"] == profile_id
    assert (converted_row["physical_value"], converted_row["conversion_status"]) == (
        6.0,
        "converted",
    )


# ---------------------------------------------------------------------------
# WebSocket ingest error paths
# ---------------------------------------------------------------------------


async def test_ws_invalid_sensor_id_returns_400(service_client):
    resp = await service_client.get("/api/v1/telemetry/ws?sensor_id=not-a-uuid&token=t")
    assert resp.status == 400


async def test_ws_binary_frame_is_rejected_but_connection_stays_open(service_client, pgsql):
    ids = await _seed(_db_uri(pgsql))
    ws = await service_client.ws_connect(
        f"/api/v1/telemetry/ws?sensor_id={ids['sensor_id']}&token=sensor-token"
    )
    try:
        await ws.send_bytes(b"\x00\x01")
        msg = await ws.receive_json()
        assert msg["code"] == "unsupported"
        await ws.send_json(
            {
                "capture_session_id": str(ids["capture_session_id"]),
                "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0}],
            }
        )
        assert (await ws.receive_json())["status"] == "accepted"
    finally:
        await ws.close()


async def test_ws_unknown_run_returns_bad_request(service_client, pgsql):
    ids = await _seed(_db_uri(pgsql))
    ws = await service_client.ws_connect(
        f"/api/v1/telemetry/ws?sensor_id={ids['sensor_id']}&token=sensor-token"
    )
    try:
        await ws.send_json(
            {
                "run_id": str(uuid4()),
                "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0}],
            }
        )
        msg = await ws.receive_json()
        assert msg == {"status": "error", "code": "bad_request", "message": "Run not found"}
        assert not ws.closed
    finally:
        await ws.close()


async def test_ws_revoked_token_closes_connection(service_client, pgsql):
    db_uri = _db_uri(pgsql)
    ids = await _seed(db_uri)
    ws = await service_client.ws_connect(
        f"/api/v1/telemetry/ws?sensor_id={ids['sensor_id']}&token=sensor-token"
    )
    conn = await asyncpg.connect(db_uri)
    try:
        await conn.execute(
            "UPDATE sensors SET token_hash = $1 WHERE id = $2",
            _token_hash("rotated"),
            ids["sensor_id"],
        )
    finally:
        await conn.close()
    await ws.send_json(
        {
            "capture_session_id": str(ids["capture_session_id"]),
            "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0}],
        }
    )
    msg = await ws.receive_json()
    assert msg["code"] == "unauthorized"
    close = await ws.receive()
    assert close.type == aiohttp.WSMsgType.CLOSE
    assert close.data == aiohttp.WSCloseCode.POLICY_VIOLATION
    await ws.close()


async def test_ws_unexpected_ingest_error_returns_internal_error(
    service_client, pgsql, monkeypatch
):
    ids = await _seed(_db_uri(pgsql))

    async def boom(self, payload, *, token):  # noqa: ANN001
        raise RuntimeError("unexpected")

    monkeypatch.setattr(TelemetryIngestService, "ingest", boom)
    ws = await service_client.ws_connect(
        f"/api/v1/telemetry/ws?sensor_id={ids['sensor_id']}&token=sensor-token"
    )
    try:
        await ws.send_json({"readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0}]})
        msg = await ws.receive_json()
        assert msg == {"status": "error", "code": "internal_error", "message": "Internal error"}
        assert not ws.closed
    finally:
        await ws.close()
