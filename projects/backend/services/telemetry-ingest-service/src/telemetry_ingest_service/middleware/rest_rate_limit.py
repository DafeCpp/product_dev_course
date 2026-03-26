"""Per-sensor fixed-window rate limiter for the REST ingest endpoint.

Two independent counters share the same window:

* **requests** — number of HTTP POST requests received from a sensor.
  Protects against clients that blast many small requests.

* **readings** — total telemetry readings across all requests.
  Protects the DB from a client that sends a few very large batches.

When either counter is exceeded ``check()`` returns ``(False, retry_after)``
so the caller can respond with HTTP 429 and a ``Retry-After`` header.

Thread safety: asyncio is single-threaded; no locking needed.
"""
from __future__ import annotations

import logging
import time
from collections import defaultdict
from dataclasses import dataclass
from uuid import UUID

logger = logging.getLogger(__name__)


@dataclass
class _Window:
    reset_at: float
    requests: int = 0
    readings: int = 0


class IngestRateLimiter:
    """Fixed-window per-sensor rate limiter for REST ingest.

    Args:
        max_requests_per_window: maximum POST requests allowed per window.
        max_readings_per_window: maximum telemetry readings allowed per window.
        window_seconds: window duration in seconds.
    """

    def __init__(
        self,
        max_requests_per_window: int,
        max_readings_per_window: int,
        window_seconds: float,
    ) -> None:
        self._max_requests = max_requests_per_window
        self._max_readings = max_readings_per_window
        self._window = window_seconds
        self._buckets: dict[UUID, _Window] = defaultdict(
            lambda: _Window(reset_at=time.monotonic() + window_seconds)
        )

    def check(self, sensor_id: UUID, readings_count: int) -> tuple[bool, int]:
        """Check and increment rate limit counters for *sensor_id*.

        Returns ``(True, 0)`` when the request is within limits.
        Returns ``(False, retry_after)`` when a limit is exceeded — counters
        are NOT modified on rejection (the request is dropped before any
        state change).

        Args:
            sensor_id: UUID of the sensor making the request.
            readings_count: number of readings in the current request.

        Returns:
            Tuple of (allowed, retry_after_seconds).
        """
        now = time.monotonic()
        bucket = self._buckets[sensor_id]

        if now >= bucket.reset_at:
            bucket.reset_at = now + self._window
            bucket.requests = 0
            bucket.readings = 0

        retry_after = max(0, int(bucket.reset_at - now))

        if bucket.requests >= self._max_requests:
            logger.warning(
                "REST ingest rate limit exceeded (requests): sensor_id=%s "
                "requests=%d limit=%d retry_after=%ds",
                sensor_id,
                bucket.requests,
                self._max_requests,
                retry_after,
            )
            return False, retry_after

        if bucket.readings + readings_count > self._max_readings:
            logger.warning(
                "REST ingest rate limit exceeded (readings): sensor_id=%s "
                "readings=%d incoming=%d limit=%d retry_after=%ds",
                sensor_id,
                bucket.readings,
                readings_count,
                self._max_readings,
                retry_after,
            )
            return False, retry_after

        bucket.requests += 1
        bucket.readings += readings_count
        return True, 0
