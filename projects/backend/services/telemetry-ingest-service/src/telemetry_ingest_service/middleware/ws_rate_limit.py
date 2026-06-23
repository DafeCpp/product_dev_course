"""Per-sensor fixed-window rate limiter for the WebSocket ingest endpoint.

Two independent counters share the same window:

* **messages** — number of WS frames received.
  Protects against clients that open a connection and blast tiny frames.

* **readings** — total telemetry readings across all frames.
  Protects the DB from a client that sends a few very large batches.

When either counter is exceeded ``check()`` returns a ``RateLimitExceeded``
dataclass instead of raising — the caller decides how to surface the error
(send a JSON error frame and keep the connection alive).

Limits are read from a shared :class:`RateLimitConfig` on every ``check()``,
so runtime updates take effect immediately.  A limit of ``0`` means *unlimited*
for that counter.

Thread safety: asyncio is single-threaded; no locking needed.
"""
from __future__ import annotations

import time
from collections import defaultdict
from dataclasses import dataclass
from uuid import UUID

from telemetry_ingest_service.middleware.rate_limit_config import RateLimitConfig


@dataclass(frozen=True)
class RateLimitExceeded:
    """Returned by ``WsRateLimiter.check`` when a limit is hit."""

    reason: str          # "messages" | "readings"
    limit: int
    retry_after: int     # seconds until the current window expires


@dataclass
class _Window:
    reset_at: float
    messages: int = 0
    readings: int = 0


class WsRateLimiter:
    """Fixed-window rate limiter keyed by sensor_id.

    Args:
        config: shared, mutable rate-limit configuration.  Limits are read
            live on every ``check()`` so runtime changes apply at once.
    """

    def __init__(self, config: RateLimitConfig) -> None:
        self._config = config
        self._buckets: dict[UUID, _Window] = defaultdict(
            lambda: _Window(reset_at=time.monotonic() + self._config.ws_window_seconds)
        )

    def check(self, sensor_id: UUID, reading_count: int) -> RateLimitExceeded | None:
        """Increment counters for *sensor_id*.

        Returns ``None`` when within limits.
        Returns ``RateLimitExceeded`` without modifying counters when a limit
        would be exceeded (the frame is rejected before any state change).
        """
        max_messages = self._config.ws_max_messages
        max_readings = self._config.ws_max_readings

        now = time.monotonic()
        bucket = self._buckets[sensor_id]

        if now >= bucket.reset_at:
            bucket.reset_at = now + self._config.ws_window_seconds
            bucket.messages = 0
            bucket.readings = 0

        retry_after = max(0, int(bucket.reset_at - now))

        # A limit of 0 means "unlimited" for that counter.
        if max_messages and bucket.messages >= max_messages:
            return RateLimitExceeded(
                reason="messages",
                limit=max_messages,
                retry_after=retry_after,
            )

        if max_readings and bucket.readings + reading_count > max_readings:
            return RateLimitExceeded(
                reason="readings",
                limit=max_readings,
                retry_after=retry_after,
            )

        bucket.messages += 1
        bucket.readings += reading_count
        return None
