"""Unit tests for IngestRateLimiter (REST per-sensor fixed-window rate limiter).

No DB or aiohttp required — pure unit tests.

The limiter reads its limits from a shared :class:`RateLimitConfig` on every
``check()``, so these tests build a config and (where relevant) mutate it
between calls to assert runtime reconfiguration.
"""
from __future__ import annotations

import time
from uuid import uuid4

import pytest

from telemetry_ingest_service.middleware.rate_limit_config import RateLimitConfig
from telemetry_ingest_service.middleware.rest_rate_limit import IngestRateLimiter


def _config(max_requests: int, max_readings: int, window: float) -> RateLimitConfig:
    """Build a config with the given REST limits (WS fields irrelevant here)."""
    return RateLimitConfig(
        rest_max_requests=max_requests,
        rest_max_readings=max_readings,
        rest_window_seconds=window,
        ws_max_messages=0,
        ws_max_readings=0,
        ws_window_seconds=1.0,
    )


def _limiter(max_requests: int, max_readings: int, window: float) -> IngestRateLimiter:
    return IngestRateLimiter(_config(max_requests, max_readings, window))


def test_rate_limiter_allows_within_limit() -> None:
    """Requests within both limits are accepted."""
    limiter = _limiter(5, 100, 60.0)
    sensor_id = uuid4()
    for _ in range(5):
        allowed, retry_after = limiter.check(sensor_id, 10)
        assert allowed is True
        assert retry_after == 0


def test_rate_limiter_blocks_over_request_limit() -> None:
    """Request counter exceeded → returns (False, retry_after > 0)."""
    limiter = _limiter(2, 100_000, 60.0)
    sensor_id = uuid4()
    limiter.check(sensor_id, 1)
    limiter.check(sensor_id, 1)

    allowed, retry_after = limiter.check(sensor_id, 1)
    assert allowed is False
    assert retry_after >= 0


def test_rate_limiter_blocks_over_readings_limit() -> None:
    """Readings counter exceeded → returns (False, retry_after)."""
    limiter = _limiter(1_000, 5, 60.0)
    sensor_id = uuid4()
    limiter.check(sensor_id, 3)  # readings = 3

    allowed, retry_after = limiter.check(sensor_id, 3)  # would be 6 > 5
    assert allowed is False
    assert retry_after >= 0


def test_rate_limiter_resets_after_window(monkeypatch: pytest.MonkeyPatch) -> None:
    """After the window expires, counters reset and new requests are accepted."""
    limiter = _limiter(1, 100, 1.0)
    sensor_id = uuid4()
    limiter.check(sensor_id, 1)  # exhaust request quota

    original_monotonic = time.monotonic
    monkeypatch.setattr(time, "monotonic", lambda: original_monotonic() + 2.0)

    allowed, retry_after = limiter.check(sensor_id, 1)  # new window
    assert allowed is True
    assert retry_after == 0


def test_rate_limiter_returns_retry_after() -> None:
    """retry_after is a non-negative integer when limit is hit."""
    limiter = _limiter(1, 100, 10.0)
    sensor_id = uuid4()
    limiter.check(sensor_id, 1)

    allowed, retry_after = limiter.check(sensor_id, 1)
    assert allowed is False
    assert isinstance(retry_after, int)
    assert retry_after >= 0


def test_rate_limiter_independent_per_sensor() -> None:
    """Different sensors have independent counters."""
    limiter = _limiter(1, 100, 60.0)
    sensor_a = uuid4()
    sensor_b = uuid4()

    limiter.check(sensor_a, 1)  # exhaust sensor_a quota

    allowed_b, _ = limiter.check(sensor_b, 1)
    assert allowed_b is True


def test_rate_limiter_rejected_request_does_not_increment_readings() -> None:
    """A rejected request must not change the readings counter."""
    limiter = _limiter(1_000, 5, 60.0)
    sensor_id = uuid4()
    limiter.check(sensor_id, 3)  # readings = 3

    limiter.check(sensor_id, 3)  # rejected (would be 6 > 5), counter stays at 3

    # A batch fitting the remaining budget (5 - 3 = 2) must succeed.
    allowed, _ = limiter.check(sensor_id, 2)
    assert allowed is True


def test_rate_limiter_multiple_sensors_do_not_interfere() -> None:
    """Many concurrent sensors each get their own full window quota."""
    limiter = _limiter(3, 30, 60.0)
    sensors = [uuid4() for _ in range(10)]
    for sensor_id in sensors:
        for _ in range(3):
            allowed, _ = limiter.check(sensor_id, 5)
            assert allowed is True
        # 4th request must be blocked
        allowed, _ = limiter.check(sensor_id, 1)
        assert allowed is False


# --- runtime reconfiguration ------------------------------------------------


def test_raising_limit_at_runtime_unblocks_without_rebuilding() -> None:
    """Mutating the shared config lifts the limit on the next check — no restart,
    and the per-sensor window state is preserved."""
    config = _config(1, 100, 60.0)
    limiter = IngestRateLimiter(config)
    sensor_id = uuid4()

    assert limiter.check(sensor_id, 1)[0] is True
    assert limiter.check(sensor_id, 1)[0] is False  # request quota exhausted

    config.rest_max_requests = 5  # operator raises the limit live

    # Same limiter, same bucket — next check is now allowed.
    assert limiter.check(sensor_id, 1)[0] is True


def test_lowering_limit_at_runtime_blocks_next_check() -> None:
    """Lowering the limit below the current count rejects the next request."""
    config = _config(10, 1000, 60.0)
    limiter = IngestRateLimiter(config)
    sensor_id = uuid4()

    for _ in range(3):
        assert limiter.check(sensor_id, 1)[0] is True

    config.rest_max_requests = 3  # now at the limit

    assert limiter.check(sensor_id, 1)[0] is False


def test_zero_request_limit_means_unlimited() -> None:
    """rest_max_requests=0 disables the request counter (unlimited)."""
    limiter = _limiter(0, 1000, 60.0)
    sensor_id = uuid4()
    for _ in range(1000):
        assert limiter.check(sensor_id, 1)[0] is True


def test_zero_readings_limit_means_unlimited() -> None:
    """rest_max_readings=0 disables the readings counter (unlimited)."""
    limiter = _limiter(1_000_000, 0, 60.0)
    sensor_id = uuid4()
    allowed, _ = limiter.check(sensor_id, 10_000_000)
    assert allowed is True


def test_counters_are_independent_when_one_is_unlimited() -> None:
    """requests=0 (unlimited) still enforces the readings cap."""
    limiter = _limiter(0, 5, 60.0)
    sensor_id = uuid4()
    assert limiter.check(sensor_id, 3)[0] is True
    assert limiter.check(sensor_id, 3)[0] is False  # 6 > 5 readings
