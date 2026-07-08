"""Tests for the WebSocket per-sensor rate limiter.

Unit tests (no DB, no aiohttp):
  - requests within limits are accepted
  - message counter exceeded → RateLimitExceeded(reason="messages")
  - readings counter exceeded → RateLimitExceeded(reason="readings")
  - window resets after expiry (both counters cleared)
  - per-sensor isolation (different sensors don't affect each other)
  - retry_after is a non-negative integer

Integration tests (with DB):
  - WS client receives rate_limited error when message limit is exhausted
  - WS connection stays alive after rate_limited (recoverable error)
  - different sensors have independent quotas
"""
from __future__ import annotations

import hashlib
import time
from uuid import UUID, uuid4

import asyncpg

from telemetry_ingest_service.middleware.rate_limit_config import RateLimitConfig
from telemetry_ingest_service.middleware.ws_rate_limit import RateLimitExceeded, WsRateLimiter


def _ws_config(max_messages: int, max_readings: int, window: float) -> RateLimitConfig:
    """Build a config with the given WS limits (REST fields irrelevant here)."""
    return RateLimitConfig(
        rest_max_requests=0,
        rest_max_readings=0,
        rest_window_seconds=60.0,
        ws_max_messages=max_messages,
        ws_max_readings=max_readings,
        ws_window_seconds=window,
        spool_flush_timeout_seconds=5.0,
        ws_max_message_bytes=1048576,
    )


def _ws_limiter(max_messages: int, max_readings: int, window: float) -> WsRateLimiter:
    return WsRateLimiter(_ws_config(max_messages, max_readings, window))


# ---------------------------------------------------------------------------
# Helpers shared with other WS tests
# ---------------------------------------------------------------------------


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
            sensor_id, project_id, _token_hash(token),
        )
        await conn.execute("INSERT INTO runs (id, project_id) VALUES ($1, $2)", run_id, project_id)
        await conn.execute(
            """
            INSERT INTO capture_sessions (id, run_id, project_id, ordinal_number, status, archived)
            VALUES ($1, $2, $3, 1, 'running', false)
            """,
            capture_session_id, run_id, project_id,
        )
    finally:
        await conn.close()


# ---------------------------------------------------------------------------
# Unit tests — no DB
# ---------------------------------------------------------------------------


def test_limiter_accepts_within_limits():
    limiter = _ws_limiter(5, 100, 60.0)
    uid = uuid4()
    for _ in range(5):
        result = limiter.check(uid, 10)
        assert result is None


def test_limiter_messages_exceeded():
    limiter = _ws_limiter(2, 10_000, 60.0)
    uid = uuid4()
    limiter.check(uid, 1)
    limiter.check(uid, 1)
    result = limiter.check(uid, 1)
    assert isinstance(result, RateLimitExceeded)
    assert result.reason == "messages"
    assert result.limit == 2


def test_limiter_readings_exceeded():
    limiter = _ws_limiter(1_000, 5, 60.0)
    uid = uuid4()
    limiter.check(uid, 3)
    result = limiter.check(uid, 3)  # would bring total to 6 > 5
    assert isinstance(result, RateLimitExceeded)
    assert result.reason == "readings"
    assert result.limit == 5


def test_limiter_window_resets(monkeypatch):
    limiter = _ws_limiter(1, 100, 1.0)
    uid = uuid4()
    limiter.check(uid, 1)

    original = time.monotonic
    monkeypatch.setattr(time, "monotonic", lambda: original() + 2.0)

    result = limiter.check(uid, 1)  # new window — must succeed
    assert result is None


def test_limiter_per_sensor_isolation():
    limiter = _ws_limiter(1, 100, 60.0)
    sensor_a = uuid4()
    sensor_b = uuid4()

    limiter.check(sensor_a, 1)  # exhausts sensor_a's message quota

    result_b = limiter.check(sensor_b, 1)  # sensor_b is unaffected
    assert result_b is None


def test_limiter_retry_after_is_non_negative():
    limiter = _ws_limiter(1, 100, 10.0)
    uid = uuid4()
    limiter.check(uid, 1)
    result = limiter.check(uid, 1)
    assert isinstance(result, RateLimitExceeded)
    assert result.retry_after >= 0


def test_limiter_rejected_frame_does_not_increment_readings():
    """A rejected frame must not change the readings counter."""
    limiter = _ws_limiter(1_000, 5, 60.0)
    uid = uuid4()
    limiter.check(uid, 3)   # readings = 3

    # This would exceed the limit; counter must stay at 3.
    limiter.check(uid, 3)   # rejected

    # A frame that fits in the remaining budget (5 - 3 = 2) must succeed.
    result = limiter.check(uid, 2)
    assert result is None


def test_ws_raising_limit_at_runtime_unblocks_without_rebuilding():
    """Mutating the shared config lifts the WS limit on the next check."""
    config = _ws_config(1, 100, 60.0)
    limiter = WsRateLimiter(config)
    uid = uuid4()

    assert limiter.check(uid, 1) is None
    assert isinstance(limiter.check(uid, 1), RateLimitExceeded)  # message quota hit

    config.ws_max_messages = 5  # raise live

    assert limiter.check(uid, 1) is None


def test_ws_zero_messages_limit_means_unlimited():
    """ws_max_messages=0 disables the message counter (unlimited)."""
    limiter = _ws_limiter(0, 1000, 60.0)
    uid = uuid4()
    for _ in range(1000):
        assert limiter.check(uid, 1) is None


def test_ws_zero_readings_limit_means_unlimited():
    """ws_max_readings=0 disables the readings counter (unlimited)."""
    limiter = _ws_limiter(1_000_000, 0, 60.0)
    uid = uuid4()
    assert limiter.check(uid, 10_000_000) is None


# ---------------------------------------------------------------------------
# Integration tests — DB required
# ---------------------------------------------------------------------------


async def test_ws_rate_limited_sends_error_frame(service_client, pgsql, monkeypatch):
    """When the message limit is 1, the second frame gets a rate_limited error."""
    tight = _ws_limiter(1, 100_000, 60.0)
    monkeypatch.setattr(
        "telemetry_ingest_service.api.routes.ws_ingest._ws_limiter",
        tight,
    )

    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()
    token = "rl-test-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    batch = {
        "run_id": str(run_id),
        "capture_session_id": str(capture_session_id),
        "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0}],
    }

    async with service_client.ws_connect(
        f"/api/v1/telemetry/ws?sensor_id={sensor_id}",
        headers={"Authorization": f"Bearer {token}"},
    ) as ws:
        # First message — accepted (quota = 1)
        await ws.send_json(batch)
        ack = await ws.receive_json()
        assert ack["status"] == "accepted"

        # Second message — rate limited
        await ws.send_json(batch)
        err = await ws.receive_json()
        assert err["status"] == "error"
        assert err["code"] == "rate_limited"
        assert "retry_after" in err

        # Connection must still be alive
        assert not ws.closed


async def test_ws_rate_limited_connection_stays_alive(service_client, pgsql, monkeypatch):
    """After a rate_limited error the client can keep the connection open."""
    tight = _ws_limiter(1, 100_000, 60.0)
    monkeypatch.setattr(
        "telemetry_ingest_service.api.routes.ws_ingest._ws_limiter",
        tight,
    )

    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()
    token = "rl-alive-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    batch = {
        "run_id": str(run_id),
        "capture_session_id": str(capture_session_id),
        "readings": [{"timestamp": "2026-01-01T00:00:01Z", "raw_value": 2.0}],
    }

    async with service_client.ws_connect(
        f"/api/v1/telemetry/ws?sensor_id={sensor_id}",
        headers={"Authorization": f"Bearer {token}"},
    ) as ws:
        await ws.send_json(batch)
        await ws.receive_json()  # accepted

        await ws.send_json(batch)
        err = await ws.receive_json()  # rate_limited
        assert err["code"] == "rate_limited"

        # Send an invalid JSON — connection must still respond (not closed)
        await ws.send_str("not-json")
        json_err = await ws.receive_json()
        assert json_err["code"] == "invalid_json"


async def test_ws_rate_limit_sensor_isolation(service_client, pgsql, monkeypatch):
    """Two sensors share a rate limiter instance but have independent quotas."""
    tight = _ws_limiter(1, 100_000, 60.0)
    monkeypatch.setattr(
        "telemetry_ingest_service.api.routes.ws_ingest._ws_limiter",
        tight,
    )

    project_id = uuid4()
    token_a = "rl-sensor-a"
    token_b = "rl-sensor-b"
    sensor_a = uuid4()
    sensor_b = uuid4()
    run_id = uuid4()
    cs_id = uuid4()

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    conn = await asyncpg.connect(db_uri)
    try:
        await conn.execute(
            "INSERT INTO sensors (id, project_id, status, token_hash) VALUES ($1, $2, 'active', $3)",
            sensor_a, project_id, _token_hash(token_a),
        )
        await conn.execute(
            "INSERT INTO sensors (id, project_id, status, token_hash) VALUES ($1, $2, 'active', $3)",
            sensor_b, project_id, _token_hash(token_b),
        )
        await conn.execute("INSERT INTO runs (id, project_id) VALUES ($1, $2)", run_id, project_id)
        await conn.execute(
            """
            INSERT INTO capture_sessions (id, run_id, project_id, ordinal_number, status, archived)
            VALUES ($1, $2, $3, 1, 'running', false)
            """,
            cs_id, run_id, project_id,
        )
    finally:
        await conn.close()

    batch = {
        "run_id": str(run_id),
        "capture_session_id": str(cs_id),
        "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0}],
    }

    # Exhaust sensor_a's quota
    async with service_client.ws_connect(
        f"/api/v1/telemetry/ws?sensor_id={sensor_a}",
        headers={"Authorization": f"Bearer {token_a}"},
    ) as ws_a:
        await ws_a.send_json(batch)
        await ws_a.receive_json()  # accepted
        await ws_a.send_json(batch)
        err = await ws_a.receive_json()
        assert err["code"] == "rate_limited"

    # sensor_b must still have full quota
    async with service_client.ws_connect(
        f"/api/v1/telemetry/ws?sensor_id={sensor_b}",
        headers={"Authorization": f"Bearer {token_b}"},
    ) as ws_b:
        await ws_b.send_json(batch)
        ack = await ws_b.receive_json()
        assert ack["status"] == "accepted"
