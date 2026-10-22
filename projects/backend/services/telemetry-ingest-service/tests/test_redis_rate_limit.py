"""Tests for the Redis-shared rate-limit buckets and the in-memory fallback.

Uses fakeredis (with Lua) by default. Set ``TELEMETRY_TEST_REDIS_URL`` to run
the same tests against a real Redis server; each test flushes that database.
"""
from __future__ import annotations

import asyncio
import os
from collections.abc import AsyncIterator
from typing import Any
from uuid import uuid4

import fakeredis
import pytest
from redis.asyncio import Redis

from telemetry_ingest_service import main as app_main
from telemetry_ingest_service.middleware.rate_limit_config import RateLimitConfig
from telemetry_ingest_service.middleware.redis_buckets import RedisBuckets, build_redis_buckets
from telemetry_ingest_service.middleware.rest_rate_limit import IngestRateLimiter
from telemetry_ingest_service.middleware.ws_rate_limit import RateLimitExceeded, WsRateLimiter

_REAL_REDIS_URL = os.environ.get("TELEMETRY_TEST_REDIS_URL")


def _config(
    *,
    rest: tuple[int, int, float] = (0, 0, 60.0),
    ws: tuple[int, int, float] = (0, 0, 1.0),
) -> RateLimitConfig:
    return RateLimitConfig(
        rest_max_requests=rest[0],
        rest_max_readings=rest[1],
        rest_window_seconds=rest[2],
        ws_max_messages=ws[0],
        ws_max_readings=ws[1],
        ws_window_seconds=ws[2],
        spool_flush_timeout_seconds=5.0,
        ws_max_message_bytes=1048576,
    )


class _Clock:
    def __init__(self) -> None:
        self.now = 1000.0

    def __call__(self) -> float:
        return self.now


@pytest.fixture
def server() -> fakeredis.FakeServer:
    return fakeredis.FakeServer()


@pytest.fixture
async def client(server: fakeredis.FakeServer) -> AsyncIterator[Redis]:
    if _REAL_REDIS_URL:
        real: Redis = Redis.from_url(_REAL_REDIS_URL)
        await real.flushdb()
        yield real
        await real.aclose()
    else:
        fake = fakeredis.FakeAsyncRedis(server=server)
        yield fake
        await fake.aclose()


def _rest(config: RateLimitConfig, client: Redis, **kwargs: Any) -> IngestRateLimiter:
    limiter = IngestRateLimiter(config)
    limiter.shared = RedisBuckets(client, **kwargs)
    return limiter


async def test_replicas_share_one_rest_limit(client: Redis) -> None:
    config = _config(rest=(3, 1000, 60.0))
    replica_a, replica_b = _rest(config, client), _rest(config, client)
    sensor = uuid4()

    assert await replica_a.acquire(sensor, 1) == (True, 0)
    assert await replica_b.acquire(sensor, 1) == (True, 0)
    assert await replica_a.acquire(sensor, 1) == (True, 0)
    allowed, retry_after = await replica_b.acquire(sensor, 1)
    assert allowed is False
    # One request drains in 60s / 3 = 20s.
    assert retry_after == 20


async def test_counters_survive_restart(client: Redis) -> None:
    config = _config(rest=(2, 1000, 60.0))
    sensor = uuid4()
    before = _rest(config, client)
    await before.acquire(sensor, 1)
    await before.acquire(sensor, 1)

    after_restart = _rest(config, client)  # fresh process: empty in-memory buckets
    allowed, _ = await after_restart.acquire(sensor, 1)
    assert allowed is False


async def test_rejection_takes_no_tokens(client: Redis) -> None:
    config = _config(rest=(2, 10, 60.0))
    limiter = _rest(config, client)
    sensor = uuid4()

    # Too many readings: rejected, and the request bucket stays untouched.
    allowed, retry_after = await limiter.acquire(sensor, 11)
    assert (allowed, retry_after) == (False, 60)
    assert await limiter.acquire(sensor, 5) == (True, 0)
    assert await limiter.acquire(sensor, 5) == (True, 0)


async def test_longer_wait_wins_and_matches_memory(client: Redis) -> None:
    config = _config(rest=(100, 10, 60.0))
    shared, local = _rest(config, client), IngestRateLimiter(config)
    sensor = uuid4()
    for limiter in (shared, local):
        assert await limiter.acquire(sensor, 8) == (True, 0)
    # 5 more readings exceed by 3: 3 * 60 / 10 = 18s.
    assert await shared.acquire(sensor, 5) == local.check(sensor, 5) == (False, 18)


async def test_zero_capacity_is_unlimited(client: Redis) -> None:
    limiter = _rest(_config(rest=(0, 0, 60.0)), client)
    sensor = uuid4()
    for _ in range(50):
        assert await limiter.acquire(sensor, 10_000) == (True, 0)


async def test_buckets_refill_over_time(client: Redis) -> None:
    limiter = _rest(_config(rest=(2, 1000, 0.5)), client)
    sensor = uuid4()
    await limiter.acquire(sensor, 1)
    await limiter.acquire(sensor, 1)
    assert (await limiter.acquire(sensor, 1))[0] is False

    await asyncio.sleep(0.35)  # drains 1.4 requests
    assert await limiter.acquire(sensor, 1) == (True, 0)
    assert (await limiter.acquire(sensor, 1))[0] is False


async def test_lowered_limit_clamps_shared_usage(client: Redis) -> None:
    config = _config(rest=(10, 1000, 60.0))
    limiter = _rest(config, client)
    sensor = uuid4()
    for _ in range(10):
        await limiter.acquire(sensor, 1)
    config.rest_max_requests = 2
    # Usage is clamped to the new capacity, so the wait is at most one window.
    assert await limiter.acquire(sensor, 1) == (False, 30)


async def test_keys_expire(client: Redis) -> None:
    config = _config(rest=(5, 1000, 1.0))
    limiter = _rest(config, client)
    sensor = uuid4()
    key = f"telemetry-ingest:rate-limit:rest:{sensor}"
    await limiter.acquire(sensor, 1)
    # Short windows keep the key an hour, so a window raised later still applies.
    assert 3590 < await client.ttl(key) <= 3600

    config.rest_window_seconds = 7200.0
    await limiter.acquire(sensor, 1)
    assert 14390 < await client.ttl(key) <= 14401


async def test_ws_replicas_share_one_limit(client: Redis) -> None:
    config = _config(ws=(2, 1000, 60.0))
    replicas = [WsRateLimiter(config), WsRateLimiter(config)]
    for replica in replicas:
        replica.shared = RedisBuckets(client)
    sensor = uuid4()

    assert await replicas[0].acquire(sensor, 1) is None
    assert await replicas[1].acquire(sensor, 1) is None
    assert await replicas[0].acquire(sensor, 1) == RateLimitExceeded(
        reason="messages", limit=2, retry_after=30
    )
    # An oversized frame waits a full window, longer than the message bucket.
    assert await replicas[1].acquire(sensor, 5000) == RateLimitExceeded(
        reason="readings", limit=1000, retry_after=60
    )


async def test_rest_and_ws_buckets_are_separate(client: Redis) -> None:
    config = _config(rest=(1, 1000, 60.0), ws=(1, 1000, 60.0))
    rest = _rest(config, client)
    ws = WsRateLimiter(config)
    ws.shared = RedisBuckets(client)
    sensor = uuid4()
    assert await rest.acquire(sensor, 1) == (True, 0)
    assert await ws.acquire(sensor, 1) is None


async def test_without_store_acquire_uses_memory() -> None:
    rest = IngestRateLimiter(_config(rest=(1, 1000, 60.0)))
    ws = WsRateLimiter(_config(ws=(1, 1000, 60.0)))
    sensor = uuid4()
    assert await rest.acquire(sensor, 1) == (True, 0)
    assert (await rest.acquire(sensor, 1))[0] is False
    assert await ws.acquire(sensor, 1) is None
    assert (await ws.acquire(sensor, 1)) is not None


@pytest.mark.skipif(bool(_REAL_REDIS_URL), reason="simulates an outage with fakeredis")
async def test_falls_back_to_memory_while_redis_is_down(
    client: Redis, server: fakeredis.FakeServer, caplog: pytest.LogCaptureFixture
) -> None:
    clock = _Clock()
    config = _config(rest=(2, 1000, 60.0))
    limiter = _rest(config, client, retry_seconds=5.0, clock=clock)
    sensor = uuid4()
    assert await limiter.acquire(sensor, 1) == (True, 0)  # shared usage: 1

    server.connected = False
    # Ingest keeps working on the replica's own buckets, which start empty.
    assert await limiter.acquire(sensor, 1) == (True, 0)
    assert await limiter.acquire(sensor, 1) == (True, 0)
    assert (await limiter.acquire(sensor, 1))[0] is False
    assert sum("Redis unavailable" in r.message for r in caplog.records) == 1

    # Redis is back, but it is not retried until the cooldown ends.
    server.connected = True
    clock.now += 4.0
    assert (await limiter.acquire(sensor, 1))[0] is False
    clock.now += 1.5
    assert await limiter.acquire(sensor, 1) == (True, 0)  # shared usage: 2
    assert any("Redis is back" in r.message for r in caplog.records)
    assert (await limiter.acquire(sensor, 1))[0] is False


@pytest.mark.skipif(bool(_REAL_REDIS_URL), reason="simulates an outage with fakeredis")
async def test_ws_falls_back_to_memory(client: Redis, server: fakeredis.FakeServer) -> None:
    limiter = WsRateLimiter(_config(ws=(1, 1000, 60.0)))
    limiter.shared = RedisBuckets(client)
    sensor = uuid4()
    server.connected = False
    assert await limiter.acquire(sensor, 1) is None
    assert await limiter.acquire(sensor, 1) is not None


async def test_unreachable_redis_falls_back_quickly() -> None:
    # Nothing listens on port 1: the connection is refused right away.
    shared = build_redis_buckets(
        "redis://127.0.0.1:1/0", timeout_seconds=0.2, retry_seconds=5.0
    )
    limiter = IngestRateLimiter(_config(rest=(1, 1000, 60.0)))
    limiter.shared = shared
    try:
        assert await asyncio.wait_for(limiter.acquire(uuid4(), 1), 2.0) == (True, 0)
    finally:
        await shared.close()


async def test_app_attaches_store_only_when_configured(
    monkeypatch: pytest.MonkeyPatch, client: Redis
) -> None:
    app: dict[str, Any] = {}
    monkeypatch.setattr(app_main.settings, "rate_limit_redis_url", None)
    await app_main._start_shared_rate_limits(app)  # type: ignore[arg-type]
    assert app_main.telemetry_api._rest_limiter.shared is None
    assert app_main.ws_api._ws_limiter.shared is None

    store = RedisBuckets(client)
    monkeypatch.setattr(app_main.settings, "rate_limit_redis_url", "redis://redis:6379/0")
    monkeypatch.setattr(app_main, "build_redis_buckets", lambda *_a, **_k: store)
    monkeypatch.setattr(store, "close", _noop_close)
    try:
        await app_main._start_shared_rate_limits(app)  # type: ignore[arg-type]
        assert app_main.telemetry_api._rest_limiter.shared is store
        assert app_main.ws_api._ws_limiter.shared is store
    finally:
        await app_main._stop_shared_rate_limits(app)  # type: ignore[arg-type]
    assert app_main.telemetry_api._rest_limiter.shared is None
    assert app_main.ws_api._ws_limiter.shared is None


async def _noop_close() -> None:
    return None
