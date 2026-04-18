"""Integration tests for sensor_error_log: logging and retrieval."""
from __future__ import annotations

import asyncio
import hashlib
from uuid import UUID, uuid4

import asyncpg
import pytest
from aiohttp import web


def _token_hash(token: str) -> bytes:
    return hashlib.sha256(token.encode("utf-8")).digest()


async def _seed_sensor(
    *,
    db_uri: str,
    project_id: UUID,
    sensor_id: UUID,
    token: str,
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
    finally:
        await conn.close()


async def _count_errors(db_uri: str, sensor_id: UUID) -> int:
    conn = await asyncpg.connect(db_uri)
    try:
        return int(
            await conn.fetchval(
                "SELECT COUNT(*) FROM sensor_error_log WHERE sensor_id = $1", sensor_id
            )
        )
    finally:
        await conn.close()


async def _fetch_errors(db_uri: str, sensor_id: UUID) -> list[dict]:
    conn = await asyncpg.connect(db_uri)
    try:
        rows = await conn.fetch(
            "SELECT * FROM sensor_error_log WHERE sensor_id = $1 ORDER BY occurred_at DESC",
            sensor_id,
        )
        return [dict(r) for r in rows]
    finally:
        await conn.close()


# ---------------------------------------------------------------------------
# Rate-limit path logs to sensor_error_log
# ---------------------------------------------------------------------------


async def test_rate_limited_request_logs_error(service_client, pgsql, monkeypatch):
    """A 429 response must produce one sensor_error_log row with error_code='rate_limited'."""
    from telemetry_ingest_service.middleware.rest_rate_limit import IngestRateLimiter

    # Limiter that blocks after 1 request.
    tight = IngestRateLimiter(
        max_requests_per_window=1,
        max_readings_per_window=100_000,
        window_seconds=60.0,
    )
    monkeypatch.setattr(
        "telemetry_ingest_service.api.routes.telemetry._rest_limiter", tight
    )

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    project_id = uuid4()
    sensor_id = uuid4()
    token = "rate-limit-test-token"

    await _seed_sensor(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
    )

    payload = {
        "sensor_id": str(sensor_id),
        "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0}],
    }

    # First request: consumes the quota (may succeed or fail for other reasons).
    await service_client.post(
        "/api/v1/telemetry",
        json=payload,
        headers={"Authorization": f"Bearer {token}"},
    )

    # Second request: must be rate-limited.
    resp = await service_client.post(
        "/api/v1/telemetry",
        json=payload,
        headers={"Authorization": f"Bearer {token}"},
    )
    assert resp.status == 429

    # Give the background task a moment to complete.
    await asyncio.sleep(0.1)

    errors = await _fetch_errors(db_uri, sensor_id)
    rate_limited_errors = [e for e in errors if e["error_code"] == "rate_limited"]
    assert len(rate_limited_errors) >= 1, "Expected at least one rate_limited error log entry"
    entry = rate_limited_errors[0]
    assert entry["endpoint"] == "rest"
    assert entry["readings_count"] == 1


async def test_successful_request_does_not_log_error(service_client, pgsql):
    """A successful 202 response must NOT produce any error_log entries."""
    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()
    token = "success-token"

    # Seed full context so the request actually succeeds.
    conn = await asyncpg.connect(db_uri)
    try:
        await conn.execute(
            "INSERT INTO sensors (id, project_id, status, token_hash) VALUES ($1, $2, 'active', $3)",
            sensor_id, project_id, _token_hash(token),
        )
        await conn.execute(
            "INSERT INTO runs (id, project_id) VALUES ($1, $2)", run_id, project_id
        )
        await conn.execute(
            "INSERT INTO capture_sessions (id, run_id, project_id, ordinal_number, status, archived) "
            "VALUES ($1, $2, $3, 1, 'running', false)",
            capture_session_id, run_id, project_id,
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

    # Give any hypothetical background task time to run.
    await asyncio.sleep(0.1)

    count = await _count_errors(db_uri, sensor_id)
    assert count == 0, "Successful requests must not produce error_log entries"


# ---------------------------------------------------------------------------
# GET /api/v1/sensors/{sensor_id}/error-log
# ---------------------------------------------------------------------------


@pytest.fixture
async def fake_auth_service(aiohttp_server):
    """Minimal auth-service stub for user-auth on the error-log endpoint."""
    app = web.Application()

    async def me(request: web.Request) -> web.Response:
        auth = request.headers.get("Authorization", "")
        if not auth.startswith("Bearer "):
            return web.json_response({"error": "Unauthorized"}, status=401)
        token = auth[7:].strip()
        if token.count(".") != 2:
            return web.json_response({"error": "Unauthorized"}, status=401)
        return web.json_response({"id": "user-1"}, status=200)

    async def members(request: web.Request) -> web.Response:
        auth = request.headers.get("Authorization", "")
        if not auth.startswith("Bearer "):
            return web.json_response({"error": "Unauthorized"}, status=401)
        return web.json_response(
            {"members": [{"user_id": "user-1", "role": "member"}]}, status=200
        )

    app.router.add_get("/auth/me", me)
    app.router.add_get("/projects/{project_id}/members", members)
    return await aiohttp_server(app)


async def test_error_log_get_returns_logged_entries(service_client, pgsql, fake_auth_service, monkeypatch):
    """GET /api/v1/sensors/{sensor_id}/error-log returns previously inserted log entries."""
    from telemetry_ingest_service.settings import settings as svc_settings

    monkeypatch.setattr(svc_settings, "auth_service_url", str(fake_auth_service.make_url("")).rstrip("/"))

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    project_id = uuid4()
    sensor_id = uuid4()
    token = "log-read-token"

    await _seed_sensor(
        db_uri=db_uri, project_id=project_id, sensor_id=sensor_id, token=token
    )

    # Manually insert error log rows.
    conn = await asyncpg.connect(db_uri)
    try:
        await conn.execute(
            """
            INSERT INTO sensor_error_log (sensor_id, error_code, error_message, endpoint, readings_count)
            VALUES ($1, 'rate_limited', 'Rate limit exceeded.', 'rest', 5)
            """,
            sensor_id,
        )
        await conn.execute(
            """
            INSERT INTO sensor_error_log (sensor_id, error_code, error_message, endpoint)
            VALUES ($1, 'unauthorized', 'Bad credentials', 'rest')
            """,
            sensor_id,
        )
    finally:
        await conn.close()

    resp = await service_client.get(
        f"/api/v1/sensors/{sensor_id}/error-log",
        headers={"Authorization": "Bearer a.b.c"},
    )
    assert resp.status == 200, await resp.text()
    payload = await resp.json()

    assert payload["sensor_id"] == str(sensor_id)
    assert payload["total"] == 2
    assert len(payload["entries"]) == 2
    assert payload["limit"] == 50
    assert payload["offset"] == 0

    codes = {e["error_code"] for e in payload["entries"]}
    assert codes == {"rate_limited", "unauthorized"}


async def test_error_log_get_pagination(service_client, pgsql, fake_auth_service, monkeypatch):
    """limit and offset query params work correctly."""
    from telemetry_ingest_service.settings import settings as svc_settings

    monkeypatch.setattr(svc_settings, "auth_service_url", str(fake_auth_service.make_url("")).rstrip("/"))

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    project_id = uuid4()
    sensor_id = uuid4()
    token = "pagination-token"

    await _seed_sensor(
        db_uri=db_uri, project_id=project_id, sensor_id=sensor_id, token=token
    )

    # Insert 5 error rows.
    conn = await asyncpg.connect(db_uri)
    try:
        for i in range(5):
            await conn.execute(
                "INSERT INTO sensor_error_log (sensor_id, error_code, endpoint) VALUES ($1, $2, 'rest')",
                sensor_id,
                f"rate_limited_{i}",
            )
    finally:
        await conn.close()

    # First page: limit=2
    resp = await service_client.get(
        f"/api/v1/sensors/{sensor_id}/error-log?limit=2&offset=0",
        headers={"Authorization": "Bearer a.b.c"},
    )
    assert resp.status == 200
    payload = await resp.json()
    assert payload["total"] == 5
    assert len(payload["entries"]) == 2
    assert payload["limit"] == 2
    assert payload["offset"] == 0

    # Second page: limit=2, offset=2
    resp2 = await service_client.get(
        f"/api/v1/sensors/{sensor_id}/error-log?limit=2&offset=2",
        headers={"Authorization": "Bearer a.b.c"},
    )
    assert resp2.status == 200
    payload2 = await resp2.json()
    assert payload2["total"] == 5
    assert len(payload2["entries"]) == 2
    assert payload2["offset"] == 2

    # Third page: offset=4 → 1 remaining entry
    resp3 = await service_client.get(
        f"/api/v1/sensors/{sensor_id}/error-log?limit=2&offset=4",
        headers={"Authorization": "Bearer a.b.c"},
    )
    assert resp3.status == 200
    payload3 = await resp3.json()
    assert len(payload3["entries"]) == 1

    # No overlap between pages.
    ids_p1 = {e["id"] for e in payload["entries"]}
    ids_p2 = {e["id"] for e in payload2["entries"]}
    assert ids_p1.isdisjoint(ids_p2)


async def test_error_log_get_requires_user_jwt(service_client, pgsql):
    """Sensor tokens (non-JWT) must be rejected with 401."""
    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    project_id = uuid4()
    sensor_id = uuid4()
    token = "plain-sensor-token"

    await _seed_sensor(
        db_uri=db_uri, project_id=project_id, sensor_id=sensor_id, token=token
    )

    resp = await service_client.get(
        f"/api/v1/sensors/{sensor_id}/error-log",
        headers={"Authorization": f"Bearer {token}"},
    )
    assert resp.status == 401


async def test_error_log_get_empty_for_sensor_with_no_errors(
    service_client, pgsql, fake_auth_service, monkeypatch
):
    """A sensor with no errors returns an empty list with total=0."""
    from telemetry_ingest_service.settings import settings as svc_settings

    monkeypatch.setattr(svc_settings, "auth_service_url", str(fake_auth_service.make_url("")).rstrip("/"))

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    project_id = uuid4()
    sensor_id = uuid4()
    token = "clean-sensor-token"

    await _seed_sensor(
        db_uri=db_uri, project_id=project_id, sensor_id=sensor_id, token=token
    )

    resp = await service_client.get(
        f"/api/v1/sensors/{sensor_id}/error-log",
        headers={"Authorization": "Bearer a.b.c"},
    )
    assert resp.status == 200
    payload = await resp.json()
    assert payload["total"] == 0
    assert payload["entries"] == []
