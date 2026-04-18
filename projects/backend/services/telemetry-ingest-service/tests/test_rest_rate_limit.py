"""Unit tests for IngestRateLimiter (REST per-sensor fixed-window rate limiter).

No DB or aiohttp required — pure unit tests.
"""
from __future__ import annotations

import time
from uuid import uuid4

import pytest

from telemetry_ingest_service.middleware.rest_rate_limit import IngestRateLimiter


def test_rate_limiter_allows_within_limit() -> None:
    """Requests within both limits are accepted."""
    limiter = IngestRateLimiter(
        max_requests_per_window=5,
        max_readings_per_window=100,
        window_seconds=60.0,
    )
    sensor_id = uuid4()
    for _ in range(5):
        allowed, retry_after = limiter.check(sensor_id, 10)
        assert allowed is True
        assert retry_after == 0


def test_rate_limiter_blocks_over_request_limit() -> None:
    """Request counter exceeded → returns (False, retry_after > 0)."""
    limiter = IngestRateLimiter(
        max_requests_per_window=2,
        max_readings_per_window=100_000,
        window_seconds=60.0,
    )
    sensor_id = uuid4()
    limiter.check(sensor_id, 1)
    limiter.check(sensor_id, 1)

    allowed, retry_after = limiter.check(sensor_id, 1)
    assert allowed is False
    assert retry_after >= 0


def test_rate_limiter_blocks_over_readings_limit() -> None:
    """Readings counter exceeded → returns (False, retry_after)."""
    limiter = IngestRateLimiter(
        max_requests_per_window=1_000,
        max_readings_per_window=5,
        window_seconds=60.0,
    )
    sensor_id = uuid4()
    limiter.check(sensor_id, 3)  # readings = 3

    allowed, retry_after = limiter.check(sensor_id, 3)  # would be 6 > 5
    assert allowed is False
    assert retry_after >= 0


def test_rate_limiter_resets_after_window(monkeypatch: pytest.MonkeyPatch) -> None:
    """After the window expires, counters reset and new requests are accepted."""
    limiter = IngestRateLimiter(
        max_requests_per_window=1,
        max_readings_per_window=100,
        window_seconds=1.0,
    )
    sensor_id = uuid4()
    limiter.check(sensor_id, 1)  # exhaust request quota

    original_monotonic = time.monotonic
    monkeypatch.setattr(time, "monotonic", lambda: original_monotonic() + 2.0)

    allowed, retry_after = limiter.check(sensor_id, 1)  # new window
    assert allowed is True
    assert retry_after == 0


def test_rate_limiter_returns_retry_after() -> None:
    """retry_after is a non-negative integer when limit is hit."""
    limiter = IngestRateLimiter(
        max_requests_per_window=1,
        max_readings_per_window=100,
        window_seconds=10.0,
    )
    sensor_id = uuid4()
    limiter.check(sensor_id, 1)

    allowed, retry_after = limiter.check(sensor_id, 1)
    assert allowed is False
    assert isinstance(retry_after, int)
    assert retry_after >= 0


def test_rate_limiter_independent_per_sensor() -> None:
    """Different sensors have independent counters."""
    limiter = IngestRateLimiter(
        max_requests_per_window=1,
        max_readings_per_window=100,
        window_seconds=60.0,
    )
    sensor_a = uuid4()
    sensor_b = uuid4()

    limiter.check(sensor_a, 1)  # exhaust sensor_a quota

    allowed_b, _ = limiter.check(sensor_b, 1)
    assert allowed_b is True


def test_rate_limiter_rejected_request_does_not_increment_readings() -> None:
    """A rejected request must not change the readings counter."""
    limiter = IngestRateLimiter(
        max_requests_per_window=1_000,
        max_readings_per_window=5,
        window_seconds=60.0,
    )
    sensor_id = uuid4()
    limiter.check(sensor_id, 3)  # readings = 3

    limiter.check(sensor_id, 3)  # rejected (would be 6 > 5), counter stays at 3

    # A batch fitting the remaining budget (5 - 3 = 2) must succeed.
    allowed, _ = limiter.check(sensor_id, 2)
    assert allowed is True


def test_rate_limiter_multiple_sensors_do_not_interfere() -> None:
    """Many concurrent sensors each get their own full window quota."""
    limiter = IngestRateLimiter(
        max_requests_per_window=3,
        max_readings_per_window=30,
        window_seconds=60.0,
    )
    sensors = [uuid4() for _ in range(10)]
    for sensor_id in sensors:
        for _ in range(3):
            allowed, _ = limiter.check(sensor_id, 5)
            assert allowed is True
        # 4th request must be blocked
        allowed, _ = limiter.check(sensor_id, 1)
        assert allowed is False
