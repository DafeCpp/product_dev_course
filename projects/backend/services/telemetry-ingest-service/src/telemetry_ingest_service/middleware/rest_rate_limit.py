"""Per-sensor token-bucket rate limiter for the REST ingest endpoint.

Two independent buckets, both refilled over ``rest_window_seconds``
(see :mod:`token_bucket`):

* **requests** — number of HTTP POST requests received from a sensor.
  Protects against clients that blast many small requests.

* **readings** — total telemetry readings across all requests.
  Protects the DB from a client that sends a few very large batches.

When either bucket is short ``check()`` returns ``(False, retry_after)``
so the caller can respond with HTTP 429 and a ``Retry-After`` header.

Limits are read from a shared :class:`RateLimitConfig` on every ``check()``,
so runtime updates take effect immediately.  A limit of ``0`` means *unlimited*
for that bucket.

Thread safety: asyncio is single-threaded; no locking needed.
"""
from __future__ import annotations

import logging
import time
from collections import defaultdict
from dataclasses import dataclass, field
from uuid import UUID

from telemetry_ingest_service.middleware.rate_limit_config import RateLimitConfig
from telemetry_ingest_service.middleware.token_bucket import TokenBucket

logger = logging.getLogger(__name__)


@dataclass
class _SensorBuckets:
    requests: TokenBucket = field(default_factory=TokenBucket)
    readings: TokenBucket = field(default_factory=TokenBucket)


class IngestRateLimiter:
    """Token-bucket per-sensor rate limiter for REST ingest.

    Args:
        config: shared, mutable rate-limit configuration.  Limits are read
            live on every ``check()`` so runtime changes apply at once.
    """

    def __init__(self, config: RateLimitConfig) -> None:
        self._config = config
        self._buckets: dict[UUID, _SensorBuckets] = defaultdict(_SensorBuckets)

    @property
    def max_requests(self) -> int:
        """Current per-window request limit (``0`` = unlimited)."""
        return self._config.rest_max_requests

    def check(self, sensor_id: UUID, readings_count: int) -> tuple[bool, int]:
        """Check and take rate-limit tokens for *sensor_id*.

        Returns ``(True, 0)`` when the request is within limits.
        Returns ``(False, retry_after)`` when a limit is exceeded — no tokens
        are taken on rejection (the request is dropped before any state
        change).

        Args:
            sensor_id: UUID of the sensor making the request.
            readings_count: number of readings in the current request.

        Returns:
            Tuple of (allowed, retry_after_seconds).
        """
        max_requests = self._config.rest_max_requests
        max_readings = self._config.rest_max_readings
        window = self._config.rest_window_seconds

        now = time.monotonic()
        buckets = self._buckets[sensor_id]

        retry_after = buckets.requests.retry_after(1, now, max_requests, window)
        if retry_after is not None:
            logger.warning(
                "REST ingest rate limit exceeded (requests): sensor_id=%s "
                "limit=%d retry_after=%ds",
                sensor_id,
                max_requests,
                retry_after,
            )
            return False, retry_after

        retry_after = buckets.readings.retry_after(readings_count, now, max_readings, window)
        if retry_after is not None:
            logger.warning(
                "REST ingest rate limit exceeded (readings): sensor_id=%s "
                "incoming=%d limit=%d retry_after=%ds",
                sensor_id,
                readings_count,
                max_readings,
                retry_after,
            )
            return False, retry_after

        buckets.requests.take(1, max_requests)
        buckets.readings.take(readings_count, max_readings)
        return True, 0
