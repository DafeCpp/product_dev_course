"""Token bucket shared by the REST and WebSocket ingest rate limiters.

A limit of ``N`` per ``window_seconds`` becomes a bucket of capacity ``N``
refilled continuously at ``N / window_seconds`` tokens per second. Sustained
throughput stays ``N`` per window, but a burst can never exceed ``N``: with a
fixed window a client could send ``N`` at the end of one window and ``N`` more
at the start of the next.

The bucket stores how many tokens are *used* (draining at the refill rate)
rather than how many are left, and the capacity and window are passed on
every call. So a limit changed at runtime applies to the current usage at
once: raising it frees headroom immediately, lowering it below the usage
blocks the next request for at most one window (usage is clamped to the new
capacity). A capacity of ``0`` means *unlimited*.
"""
from __future__ import annotations

import math
from dataclasses import dataclass


@dataclass
class TokenBucket:
    used: float = 0.0
    updated_at: float | None = None

    def _drain(self, now: float, capacity: int, window_seconds: float) -> None:
        if self.updated_at is not None:
            elapsed = max(0.0, now - self.updated_at)
            self.used = max(0.0, self.used - elapsed * capacity / window_seconds)
        # A lowered limit never leaves more than one full bucket to drain, so a
        # live config change delays a sensor by at most one window.
        self.used = min(self.used, float(capacity))
        self.updated_at = now

    def retry_after(
        self, cost: int, now: float, capacity: int, window_seconds: float
    ) -> int | None:
        """Return ``None`` when *cost* tokens are available, else seconds to wait.

        Does not take the tokens; call :meth:`take` once every bucket of the
        request has room, so a rejected request leaves all buckets unchanged.
        """
        if not capacity:
            return None
        self._drain(now, capacity, window_seconds)
        if self.used + cost <= capacity:
            return None
        if cost > capacity:
            # Can never fit: wait at least one full window, as before.
            return max(1, math.ceil(window_seconds))
        excess = self.used + cost - capacity
        return max(1, math.ceil(excess * window_seconds / capacity))

    def take(self, cost: int, capacity: int) -> None:
        if capacity:
            self.used += cost
