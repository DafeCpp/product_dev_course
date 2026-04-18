"""In-memory fixed-window rate limiter for telemetry export endpoints.

Each user gets a counter that resets every ``window_seconds``.  When the limit
is exceeded the caller receives a ``web.HTTPTooManyRequests`` response with the
standard rate-limit headers.

Thread safety: asyncio single-threaded; no locking needed.
"""
from __future__ import annotations

import time
from collections import defaultdict
from dataclasses import dataclass, field
from uuid import UUID

from aiohttp import web


@dataclass
class _Window:
    reset_at: float
    count: int = 0


class ExportRateLimiter:
    """Fixed-window rate limiter keyed by user_id."""

    def __init__(self, max_requests: int, window_seconds: float) -> None:
        self._max = max_requests
        self._window = window_seconds
        self._buckets: dict[UUID, _Window] = defaultdict(
            lambda: _Window(reset_at=time.monotonic() + window_seconds)
        )

    def check(self, user_id: UUID) -> None:
        """Increment the counter for *user_id* or raise HTTP 429.

        Raises ``web.HTTPTooManyRequests`` with ``Retry-After``,
        ``X-RateLimit-Limit``, ``X-RateLimit-Remaining``, and
        ``X-RateLimit-Reset`` headers when the limit is exceeded.
        """
        now = time.monotonic()
        bucket = self._buckets[user_id]

        if now >= bucket.reset_at:
            bucket.reset_at = now + self._window
            bucket.count = 0

        bucket.count += 1
        remaining = max(0, self._max - bucket.count)
        retry_after = max(0, int(bucket.reset_at - now))

        headers = {
            "X-RateLimit-Limit": str(self._max),
            "X-RateLimit-Remaining": str(remaining),
            "X-RateLimit-Reset": str(retry_after),
        }

        if bucket.count > self._max:
            raise web.HTTPTooManyRequests(
                text="Export rate limit exceeded. Try again later.",
                headers={**headers, "Retry-After": str(retry_after)},
            )
