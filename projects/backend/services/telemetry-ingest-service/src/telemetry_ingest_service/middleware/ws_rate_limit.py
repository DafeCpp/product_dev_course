"""Per-sensor token-bucket rate limiter for the WebSocket ingest endpoint.

Two independent buckets, both refilled over ``ws_window_seconds``
(see :mod:`token_bucket`):

* **messages** — number of WS frames received.
  Protects against clients that open a connection and blast tiny frames.

* **readings** — total telemetry readings across all frames.
  Protects the DB from a client that sends a few very large batches.

When either bucket is short ``check()`` returns a ``RateLimitExceeded``
dataclass instead of raising — the caller decides how to surface the error
(send a JSON error frame and keep the connection alive).

Limits are read from a shared :class:`RateLimitConfig` on every ``check()``,
so runtime updates take effect immediately.  A limit of ``0`` means *unlimited*
for that bucket.

Thread safety: asyncio is single-threaded; no locking needed.
"""
from __future__ import annotations

import time
from collections import defaultdict
from dataclasses import dataclass, field
from uuid import UUID

from telemetry_ingest_service.middleware.rate_limit_config import RateLimitConfig
from telemetry_ingest_service.middleware.token_bucket import TokenBucket


@dataclass(frozen=True)
class RateLimitExceeded:
    """Returned by ``WsRateLimiter.check`` when a limit is hit."""

    reason: str          # "messages" | "readings"
    limit: int
    retry_after: int     # seconds until enough tokens are refilled


@dataclass
class _SensorBuckets:
    messages: TokenBucket = field(default_factory=TokenBucket)
    readings: TokenBucket = field(default_factory=TokenBucket)


class WsRateLimiter:
    """Token-bucket rate limiter keyed by sensor_id.

    Args:
        config: shared, mutable rate-limit configuration.  Limits are read
            live on every ``check()`` so runtime changes apply at once.
    """

    def __init__(self, config: RateLimitConfig) -> None:
        self._config = config
        self._buckets: dict[UUID, _SensorBuckets] = defaultdict(_SensorBuckets)

    def check(self, sensor_id: UUID, reading_count: int) -> RateLimitExceeded | None:
        """Take tokens for *sensor_id*.

        Returns ``None`` when within limits.
        Returns ``RateLimitExceeded`` without taking tokens when a limit
        would be exceeded (the frame is rejected before any state change).
        """
        max_messages = self._config.ws_max_messages
        max_readings = self._config.ws_max_readings
        window = self._config.ws_window_seconds

        now = time.monotonic()
        buckets = self._buckets[sensor_id]

        # Check both buckets so retry_after covers the longer of the two waits.
        messages_wait = buckets.messages.retry_after(1, now, max_messages, window)
        readings_wait = buckets.readings.retry_after(reading_count, now, max_readings, window)

        if messages_wait is not None and (readings_wait is None or messages_wait >= readings_wait):
            return RateLimitExceeded(reason="messages", limit=max_messages, retry_after=messages_wait)

        if readings_wait is not None:
            return RateLimitExceeded(reason="readings", limit=max_readings, retry_after=readings_wait)

        buckets.messages.take(1, max_messages)
        buckets.readings.take(reading_count, max_readings)
        return None
