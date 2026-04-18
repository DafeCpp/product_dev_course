"""Per-sensor fixed-window rate limiter for the WebSocket ingest endpoint.

Two independent counters share the same window:

* **messages** — number of WS frames received.
  Protects against clients that open a connection and blast tiny frames.

* **readings** — total telemetry readings across all frames.
  Protects the DB from a client that sends a few very large batches.

When either counter is exceeded ``check()`` returns a ``RateLimitExceeded``
dataclass instead of raising — the caller decides how to surface the error
(send a JSON error frame and keep the connection alive).

Thread safety: asyncio is single-threaded; no locking needed.
"""
from __future__ import annotations

import time
from collections import defaultdict
from dataclasses import dataclass
from uuid import UUID


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
        max_messages: maximum WS frames per window.
        max_readings: maximum telemetry readings per window.
        window_seconds: window duration in seconds.
    """

    def __init__(
        self,
        max_messages: int,
        max_readings: int,
        window_seconds: float,
    ) -> None:
        self._max_messages = max_messages
        self._max_readings = max_readings
        self._window = window_seconds
        self._buckets: dict[UUID, _Window] = defaultdict(
            lambda: _Window(reset_at=time.monotonic() + window_seconds)
        )

    def check(self, sensor_id: UUID, reading_count: int) -> RateLimitExceeded | None:
        """Increment counters for *sensor_id*.

        Returns ``None`` when within limits.
        Returns ``RateLimitExceeded`` without modifying counters when a limit
        would be exceeded (the frame is rejected before any state change).
        """
        now = time.monotonic()
        bucket = self._buckets[sensor_id]

        if now >= bucket.reset_at:
            bucket.reset_at = now + self._window
            bucket.messages = 0
            bucket.readings = 0

        retry_after = max(0, int(bucket.reset_at - now))

        if bucket.messages >= self._max_messages:
            return RateLimitExceeded(
                reason="messages",
                limit=self._max_messages,
                retry_after=retry_after,
            )

        if bucket.readings + reading_count > self._max_readings:
            return RateLimitExceeded(
                reason="readings",
                limit=self._max_readings,
                retry_after=retry_after,
            )

        bucket.messages += 1
        bucket.readings += reading_count
        return None
