from __future__ import annotations

import hashlib
import json
from datetime import datetime, timezone
from uuid import UUID, uuid4

import pytest
import asyncpg
from aiohttp import web


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
            """
            INSERT INTO sensors (id, project_id, status, token_hash)
            VALUES ($1, $2, 'active', $3)
            """,
            sensor_id,
            project_id,
            _token_hash(token),
        )
        await conn.execute(
            """
            INSERT INTO runs (id, project_id)
            VALUES ($1, $2)
            """,
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


async def test_ingest_happy_path(service_client, pgsql):
    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()
    token = "test-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    ts = datetime(2026, 1, 1, 0, 0, 0, tzinfo=timezone.utc)
    resp = await service_client.post(
        "/api/v1/telemetry",
        json={
            "sensor_id": str(sensor_id),
            "run_id": str(run_id),
            "capture_session_id": str(capture_session_id),
            "meta": {"device_id": "d1"},
            "readings": [
                {"timestamp": ts.isoformat().replace("+00:00", "Z"), "raw_value": 1.23, "meta": {"signal": "a"}}
            ],
        },
        headers={"Authorization": f"Bearer {token}"},
    )
    assert resp.status == 202
    payload = await resp.json()
    assert payload["status"] == "accepted"
    assert payload["accepted"] == 1

    conn = await asyncpg.connect(db_uri)
    try:
        count = await conn.fetchval("SELECT COUNT(*) FROM telemetry_records WHERE sensor_id = $1", sensor_id)
        assert int(count) == 1
        hb = await conn.fetchval("SELECT last_heartbeat FROM sensors WHERE id = $1", sensor_id)
        assert hb is not None
    finally:
        await conn.close()


async def test_ingest_marks_late_data_in_meta(service_client, pgsql):
    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()
    token = "test-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    conn = await asyncpg.connect(db_uri)
    try:
        await conn.execute(
            "UPDATE capture_sessions SET status = 'succeeded' WHERE id = $1 AND project_id = $2",
            capture_session_id,
            project_id,
        )
    finally:
        await conn.close()

    resp = await service_client.post(
        "/api/v1/telemetry",
        json={
            "sensor_id": str(sensor_id),
            "run_id": str(run_id),
            "capture_session_id": str(capture_session_id),
            "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0}],
        },
        headers={"Authorization": f"Bearer {token}"},
    )
    assert resp.status == 202

    conn = await asyncpg.connect(db_uri)
    try:
        row = await conn.fetchrow(
            """
            SELECT capture_session_id, meta
            FROM telemetry_records
            WHERE run_id = $1
            ORDER BY id DESC
            LIMIT 1
            """,
            run_id,
        )
        assert row is not None
        # After session is stopped, service must NOT attach new records to it.
        assert row["capture_session_id"] is None
        meta = row["meta"]
        assert meta is not None
        if isinstance(meta, str):
            meta = json.loads(meta)
        assert meta["__system"]["late"] is True
        assert meta["__system"]["capture_session_attached"] is False
        assert meta["__system"]["capture_session_id"] == str(capture_session_id)
    finally:
        await conn.close()


async def test_ingest_auto_attaches_active_capture_session_by_run_id(service_client, pgsql):
    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()
    token = "test-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    # Client sends run_id only; service should attach active capture_session_id.
    resp = await service_client.post(
        "/api/v1/telemetry",
        json={
            "sensor_id": str(sensor_id),
            "run_id": str(run_id),
            "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0}],
        },
        headers={"Authorization": f"Bearer {token}"},
    )
    assert resp.status == 202

    conn = await asyncpg.connect(db_uri)
    try:
        row = await conn.fetchrow(
            """
            SELECT capture_session_id, meta
            FROM telemetry_records
            WHERE run_id = $1
            ORDER BY id DESC
            LIMIT 1
            """,
            run_id,
        )
        assert row is not None
        assert str(row["capture_session_id"]) == str(capture_session_id)
        meta = row["meta"]
        if isinstance(meta, str):
            meta = json.loads(meta)
        assert meta["__system"]["capture_session_auto_attached"] is True
    finally:
        await conn.close()


async def test_ingest_auto_infers_capture_session_from_sensor_assignment(service_client, pgsql):
    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()
    token = "test-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    # Client omits run_id and capture_session_id; service should infer.
    resp = await service_client.post(
        "/api/v1/telemetry",
        json={
            "sensor_id": str(sensor_id),
            "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0}],
        },
        headers={"Authorization": f"Bearer {token}"},
    )
    assert resp.status == 202

    conn = await asyncpg.connect(db_uri)
    try:
        row = await conn.fetchrow(
            """
            SELECT run_id, capture_session_id, meta
            FROM telemetry_records
            WHERE sensor_id = $1
            ORDER BY id DESC
            LIMIT 1
            """,
            sensor_id,
        )
        assert row is not None
        assert str(row["run_id"]) == str(run_id)
        assert str(row["capture_session_id"]) == str(capture_session_id)
        meta = row["meta"]
        if isinstance(meta, str):
            meta = json.loads(meta)
        assert meta["__system"]["capture_session_inferred_from_project"] is True
    finally:
        await conn.close()


async def test_ingest_rejects_too_large_meta_400(service_client, pgsql):
    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()
    token = "test-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    # Create batch meta > 64KB
    big = "x" * (70 * 1024)
    resp = await service_client.post(
        "/api/v1/telemetry",
        json={
            "sensor_id": str(sensor_id),
            "run_id": str(run_id),
            "capture_session_id": str(capture_session_id),
            "meta": {"big": big},
            "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0}],
        },
        headers={"Authorization": f"Bearer {token}"},
    )
    assert resp.status == 400


async def test_stream_requires_token_401(service_client):
    resp = await service_client.get(
        "/api/v1/telemetry/stream?sensor_id=00000000-0000-0000-0000-000000000000"
    )
    assert resp.status == 401


async def test_stream_emits_inserted_records(service_client, pgsql):
    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()
    token = "test-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    conn = await asyncpg.connect(db_uri)
    try:
        await conn.execute(
            """
            INSERT INTO telemetry_records (
                project_id, sensor_id, run_id, capture_session_id,
                timestamp, raw_value, physical_value, meta, conversion_status
            )
            VALUES ($1, $2, $3, $4, now(), 1.23, NULL, '{}'::jsonb, 'raw_only')
            """,
            project_id,
            sensor_id,
            run_id,
            capture_session_id,
        )
    finally:
        await conn.close()

    resp = await service_client.get(
        f"/api/v1/telemetry/stream?sensor_id={sensor_id}&since_id=0&max_events=1&idle_timeout_seconds=5",
        headers={"Authorization": f"Bearer {token}"},
    )
    assert resp.status == 200

    # Read until we receive one SSE "data:" block
    raw = await resp.content.readuntil(b"\n\n")
    assert b"event: telemetry" in raw
    assert b"data: " in raw


async def test_stream_accepts_query_token_for_sse_clients(service_client, pgsql):
    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()
    token = "query-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    conn = await asyncpg.connect(db_uri)
    try:
        await conn.execute(
            """
            INSERT INTO telemetry_records (
                project_id, sensor_id, run_id, capture_session_id,
                timestamp, raw_value, physical_value, meta, conversion_status
            )
            VALUES ($1, $2, $3, $4, now(), 1.23, NULL, '{}'::jsonb, 'raw_only')
            """,
            project_id,
            sensor_id,
            run_id,
            capture_session_id,
        )
    finally:
        await conn.close()

    resp = await service_client.get(
        f"/api/v1/telemetry/stream?sensor_id={sensor_id}&since_id=0&max_events=1"
        f"&idle_timeout_seconds=5&access_token={token}"
    )
    assert resp.status == 200

    raw = await resp.content.readuntil(b"\n\n")
    assert b"event: telemetry" in raw
    assert b"data: " in raw


@pytest.fixture
async def fake_auth_service(aiohttp_server):
    """
    Minimal auth-service stub for telemetry stream user-auth tests.
    Implements:
      - GET /auth/me
      - GET /projects/{project_id}/members
    """
    app = web.Application()

    async def me(request: web.Request) -> web.Response:
        auth = request.headers.get("Authorization", "")
        if not auth.startswith("Bearer "):
            return web.json_response({"error": "Unauthorized"}, status=401)
        token = auth[7:].strip()
        # accept any token that looks like a JWT
        if token.count(".") != 2:
            return web.json_response({"error": "Unauthorized"}, status=401)
        return web.json_response({"id": "user-1", "username": "u1", "email": "u1@example.com"}, status=200)

    async def members(request: web.Request) -> web.Response:
        auth = request.headers.get("Authorization", "")
        if not auth.startswith("Bearer "):
            return web.json_response({"error": "Unauthorized"}, status=401)
        return web.json_response(
            {
                "members": [
                    {"user_id": "user-1", "role": "member"},
                ]
            },
            status=200,
        )

    app.router.add_get("/auth/me", me)
    app.router.add_get("/projects/{project_id}/members", members)
    return await aiohttp_server(app)


async def test_stream_user_token_allows_project_member(service_client, pgsql, fake_auth_service, monkeypatch):
    from telemetry_ingest_service.settings import settings

    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()
    token = "sensor-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    conn = await asyncpg.connect(db_uri)
    try:
        await conn.execute(
            """
            INSERT INTO telemetry_records (
                project_id, sensor_id, run_id, capture_session_id,
                timestamp, raw_value, physical_value, meta, conversion_status
            )
            VALUES ($1, $2, $3, $4, now(), 1.23, NULL, '{}'::jsonb, 'raw_only')
            """,
            project_id,
            sensor_id,
            run_id,
            capture_session_id,
        )
    finally:
        await conn.close()

    monkeypatch.setattr(settings, "auth_service_url", str(fake_auth_service.make_url("")).rstrip("/"))

    # JWT-like token triggers user-auth path
    user_jwt = "a.b.c"
    resp = await service_client.get(
        f"/api/v1/telemetry/stream?sensor_id={sensor_id}&since_id=0&max_events=1&idle_timeout_seconds=5",
        headers={"Authorization": f"Bearer {user_jwt}"},
    )
    assert resp.status == 200
    raw = await resp.content.readuntil(b"\n\n")
    assert b"event: telemetry" in raw
    assert b"data: " in raw


async def test_ingest_invalid_token_401(service_client, pgsql):
    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()
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

    resp = await service_client.post(
        "/api/v1/telemetry",
        json={"sensor_id": str(sensor_id), "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0}]},
        headers={"Authorization": "Bearer wrong-token"},
    )
    assert resp.status == 401


async def test_ingest_run_capture_mismatch_400(service_client, pgsql):
    project_id = uuid4()
    sensor_id = uuid4()
    token = "t1"

    run_a = uuid4()
    run_b = uuid4()
    capture_b = uuid4()

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    # seed sensor + run_b + capture_b (belongs to run_b) and run_a exists too
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_b,
        capture_session_id=capture_b,
    )
    conn = await asyncpg.connect(db_uri)
    try:
        await conn.execute("INSERT INTO runs (id, project_id) VALUES ($1, $2)", run_a, project_id)
    finally:
        await conn.close()

    resp = await service_client.post(
        "/api/v1/telemetry",
        json={
            "sensor_id": str(sensor_id),
            "run_id": str(run_a),
            "capture_session_id": str(capture_b),
            "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0}],
        },
        headers={"Authorization": f"Bearer {token}"},
    )
    assert resp.status == 400


async def test_ingest_rejects_archived_run_400(service_client, pgsql):
    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()
    token = "test-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    conn = await asyncpg.connect(db_uri)
    try:
        await conn.execute(
            "UPDATE runs SET status = 'archived' WHERE id = $1 AND project_id = $2",
            run_id,
            project_id,
        )
    finally:
        await conn.close()

    resp = await service_client.post(
        "/api/v1/telemetry",
        json={
            "sensor_id": str(sensor_id),
            "run_id": str(run_id),
            "capture_session_id": str(capture_session_id),
            "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0}],
        },
        headers={"Authorization": f"Bearer {token}"},
    )
    assert resp.status == 400


async def test_ingest_rejects_archived_capture_session_400(service_client, pgsql):
    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()
    token = "test-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    conn = await asyncpg.connect(db_uri)
    try:
        await conn.execute(
            "UPDATE capture_sessions SET archived = true WHERE id = $1 AND project_id = $2",
            capture_session_id,
            project_id,
        )
    finally:
        await conn.close()

    resp = await service_client.post(
        "/api/v1/telemetry",
        json={
            "sensor_id": str(sensor_id),
            "run_id": str(run_id),
            "capture_session_id": str(capture_session_id),
            "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0}],
        },
        headers={"Authorization": f"Bearer {token}"},
    )
    assert resp.status == 400

