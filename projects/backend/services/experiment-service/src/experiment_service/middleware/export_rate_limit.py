"""In-memory fixed-window rate limiter keyed by user_id.

Used by the telemetry export endpoints (static limit from settings) and by
the heavy API endpoints (limit read from ``QOS_CONFIG`` on every check, so a
change saved in config-service applies without a restart).

Each user gets a counter that resets every ``window_seconds``.  When the limit
is exceeded the caller receives a ``web.HTTPTooManyRequests`` response with the
standard rate-limit headers.

Thread safety: asyncio single-threaded; no locking needed.
"""
from __future__ import annotations

import time
from collections import defaultdict
from collections.abc import Callable
from dataclasses import dataclass, field
from uuid import UUID

from aiohttp import web


@dataclass
class _Window:
    reset_at: float
    count: int = 0


class ExportRateLimiter:
    """Fixed-window rate limiter keyed by user_id."""

    def __init__(
        self,
        max_requests: int | Callable[[], int],
        window_seconds: float,
        message: str = "Export rate limit exceeded. Try again later.",
    ) -> None:
        if callable(max_requests):
            self._limit = max_requests
        else:
            static_limit = max_requests
            self._limit = lambda: static_limit
        self._window = window_seconds
        self._message = message
        self._buckets: dict[UUID, _Window] = defaultdict(
            lambda: _Window(reset_at=time.monotonic() + window_seconds)
        )

    def check(self, user_id: UUID) -> None:
        """Increment the counter for *user_id* or raise HTTP 429.

        Raises ``web.HTTPTooManyRequests`` with ``Retry-After``,
        ``X-RateLimit-Limit``, ``X-RateLimit-Remaining``, and
        ``X-RateLimit-Reset`` headers when the limit is exceeded.
        """
        limit = self._limit()
        now = time.monotonic()
        bucket = self._buckets[user_id]

        if now >= bucket.reset_at:
            bucket.reset_at = now + self._window
            bucket.count = 0

        bucket.count += 1
        remaining = max(0, limit - bucket.count)
        retry_after = max(0, int(bucket.reset_at - now))

        headers = {
            "X-RateLimit-Limit": str(limit),
            "X-RateLimit-Remaining": str(remaining),
            "X-RateLimit-Reset": str(retry_after),
        }

        if bucket.count > limit:
            raise web.HTTPTooManyRequests(
                text=self._message,
                headers={**headers, "Retry-After": str(retry_after)},
            )
