"""Burst scenarios for the token-bucket REST and WS ingest limiters.

A fixed window let a sensor send the full limit at the end of one window and
again at the start of the next; the token bucket caps any burst at the limit
while keeping the sustained rate of ``limit`` per window.
"""
from __future__ import annotations

import time
from uuid import uuid4

import pytest

from telemetry_ingest_service.middleware.rate_limit_config import RateLimitConfig
from telemetry_ingest_service.middleware.rest_rate_limit import IngestRateLimiter
from telemetry_ingest_service.middleware.ws_rate_limit import WsRateLimiter


def _config(**overrides: float) -> RateLimitConfig:
    values: dict = {
        "rest_max_requests": 0,
        "rest_max_readings": 0,
        "rest_window_seconds": 1.0,
        "ws_max_messages": 0,
        "ws_max_readings": 0,
        "ws_window_seconds": 1.0,
        "spool_flush_timeout_seconds": 5.0,
        "ws_max_message_bytes": 1048576,
    }
    values.update(overrides)
    return RateLimitConfig(**values)


class _Clock:
    def __init__(self) -> None:
        self.now = 1000.0

    def __call__(self) -> float:
        return self.now


@pytest.fixture
def clock(monkeypatch) -> _Clock:
    c = _Clock()
    monkeypatch.setattr(time, "monotonic", c)
    return c


def test_rest_burst_across_window_boundary_is_capped(clock: _Clock) -> None:
    limiter = IngestRateLimiter(_config(rest_max_requests=10, rest_window_seconds=1.0))
    sensor = uuid4()

    clock.now += 0.99
    assert all(limiter.check(sensor, 1)[0] for _ in range(10))

    # Fixed window would reset here and allow 10 more; only ~0.2 tokens refilled.
    clock.now += 0.02
    allowed, retry_after = limiter.check(sensor, 1)
    assert not allowed
    assert retry_after == 1


def test_rest_sustained_rate_is_kept(clock: _Clock) -> None:
    limiter = IngestRateLimiter(_config(rest_max_requests=10, rest_window_seconds=1.0))
    sensor = uuid4()

    # Exactly the configured rate (one request every 0.1 s) for 10 windows.
    for _ in range(100):
        assert limiter.check(sensor, 1) == (True, 0)
        clock.now += 0.1


def test_rest_readings_bucket_refills_gradually(clock: _Clock) -> None:
    limiter = IngestRateLimiter(_config(rest_max_readings=100, rest_window_seconds=10.0))
    sensor = uuid4()

    assert limiter.check(sensor, 100) == (True, 0)
    allowed, retry_after = limiter.check(sensor, 50)
    assert not allowed
    assert retry_after == 5  # 50 readings at 10 per second

    clock.now += 5.0
    assert limiter.check(sensor, 50) == (True, 0)


def test_rest_batch_larger_than_limit_waits_a_window(clock: _Clock) -> None:
    limiter = IngestRateLimiter(_config(rest_max_readings=10, rest_window_seconds=30.0))
    allowed, retry_after = limiter.check(uuid4(), 11)
    assert not allowed
    assert retry_after == 30


def test_rest_rejection_takes_no_tokens(clock: _Clock) -> None:
    limiter = IngestRateLimiter(
        _config(rest_max_requests=2, rest_max_readings=10, rest_window_seconds=60.0)
    )
    sensor = uuid4()

    assert not limiter.check(sensor, 11)[0]  # readings reject: request token kept
    assert limiter.check(sensor, 5) == (True, 0)
    assert limiter.check(sensor, 5) == (True, 0)
    assert not limiter.check(sensor, 0)[0]


def test_rest_lowered_limit_counts_current_usage(clock: _Clock) -> None:
    config = _config(rest_max_requests=100, rest_window_seconds=1.0)
    limiter = IngestRateLimiter(config)
    sensor = uuid4()
    assert limiter.check(sensor, 1)[0]

    config.rest_max_requests = 2  # one of the two is already used
    assert limiter.check(sensor, 1)[0]
    assert not limiter.check(sensor, 1)[0]

    clock.now += 0.5  # drains at the new rate: 2 per second
    assert limiter.check(sensor, 1)[0]


def test_rest_sharply_lowered_limit_blocks_at_most_one_window(clock: _Clock) -> None:
    config = _config(rest_max_requests=600, rest_window_seconds=60.0)
    limiter = IngestRateLimiter(config)
    sensor = uuid4()
    assert all(limiter.check(sensor, 1)[0] for _ in range(600))

    config.rest_max_requests = 1
    allowed, retry_after = limiter.check(sensor, 1)
    assert not allowed
    assert retry_after == 60

    clock.now += 60.0
    assert limiter.check(sensor, 1) == (True, 0)


def test_rest_retry_after_is_the_longer_wait(clock: _Clock) -> None:
    limiter = IngestRateLimiter(
        _config(rest_max_requests=10, rest_max_readings=100, rest_window_seconds=60.0)
    )
    sensor = uuid4()
    assert all(limiter.check(sensor, 10)[0] for _ in range(10))

    # Requests refill in 6 s, but 100 readings need the full 60 s.
    allowed, retry_after = limiter.check(sensor, 100)
    assert not allowed
    assert retry_after == 60


def test_ws_retry_after_is_the_longer_wait(clock: _Clock) -> None:
    limiter = WsRateLimiter(
        _config(ws_max_messages=10, ws_max_readings=100, ws_window_seconds=60.0)
    )
    sensor = uuid4()
    assert all(limiter.check(sensor, 10) is None for _ in range(10))

    hit = limiter.check(sensor, 100)
    assert hit is not None
    assert hit.reason == "readings"
    assert hit.limit == 100
    assert hit.retry_after == 60


def test_rest_zero_means_unlimited(clock: _Clock) -> None:
    limiter = IngestRateLimiter(_config())
    sensor = uuid4()
    assert all(limiter.check(sensor, 10_000) == (True, 0) for _ in range(1000))


def test_ws_burst_across_window_boundary_is_capped(clock: _Clock) -> None:
    limiter = WsRateLimiter(_config(ws_max_messages=5, ws_window_seconds=1.0))
    sensor = uuid4()

    clock.now += 0.99
    assert all(limiter.check(sensor, 1) is None for _ in range(5))

    clock.now += 0.02
    hit = limiter.check(sensor, 1)
    assert hit is not None
    assert hit.reason == "messages"
    assert hit.limit == 5
    assert hit.retry_after == 1

    clock.now += 0.2  # one token at 5 per second
    assert limiter.check(sensor, 1) is None
