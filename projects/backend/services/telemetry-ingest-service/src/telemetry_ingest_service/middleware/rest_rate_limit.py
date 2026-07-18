"""Per-sensor fixed-window rate limiter for the REST ingest endpoint.

Two independent counters share the same window:

* **requests** — number of HTTP POST requests received from a sensor.
  Protects against clients that blast many small requests.

* **readings** — total telemetry readings across all requests.
  Protects the DB from a client that sends a few very large batches.

When either counter is exceeded ``check()`` returns ``(False, retry_after)``
so the caller can respond with HTTP 429 and a ``Retry-After`` header.

Limits are read from a shared :class:`RateLimitConfig` on every ``check()``,
so runtime updates take effect immediately.  A limit of ``0`` means *unlimited*
for that counter.

Thread safety: asyncio is single-threaded; no locking needed.
"""
from __future__ import annotations

import logging
import time
from collections import defaultdict
from dataclasses import dataclass
from uuid import UUID

from telemetry_ingest_service.middleware.rate_limit_config import RateLimitConfig

logger = logging.getLogger(__name__)


@dataclass
class _Window:
    reset_at: float
    requests: int = 0
    readings: int = 0


class IngestRateLimiter:
    """Fixed-window per-sensor rate limiter for REST ingest.

    Args:
        config: shared, mutable rate-limit configuration.  Limits are read
            live on every ``check()`` so runtime changes apply at once.
    """

    def __init__(self, config: RateLimitConfig) -> None:
        self._config = config
        self._buckets: dict[UUID, _Window] = defaultdict(
            lambda: _Window(reset_at=time.monotonic() + self._config.rest_window_seconds)
        )

    @property
    def max_requests(self) -> int:
        """Current per-window request limit (``0`` = unlimited)."""
        return self._config.rest_max_requests

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
        max_requests = self._config.rest_max_requests
        max_readings = self._config.rest_max_readings

        now = time.monotonic()
        bucket = self._buckets[sensor_id]

        if now >= bucket.reset_at:
            bucket.reset_at = now + self._config.rest_window_seconds
            bucket.requests = 0
            bucket.readings = 0

        retry_after = max(0, int(bucket.reset_at - now))

        # A limit of 0 means "unlimited" for that counter.
        if max_requests and bucket.requests >= max_requests:
            logger.warning(
                "REST ingest rate limit exceeded (requests): sensor_id=%s "
                "requests=%d limit=%d retry_after=%ds",
                sensor_id,
                bucket.requests,
                max_requests,
                retry_after,
            )
            return False, retry_after

        if max_readings and bucket.readings + readings_count > max_readings:
            logger.warning(
                "REST ingest rate limit exceeded (readings): sensor_id=%s "
                "readings=%d incoming=%d limit=%d retry_after=%ds",
                sensor_id,
                bucket.readings,
                readings_count,
                max_readings,
                retry_after,
            )
            return False, retry_after

        bucket.requests += 1
        bucket.readings += readings_count
        return True, 0
