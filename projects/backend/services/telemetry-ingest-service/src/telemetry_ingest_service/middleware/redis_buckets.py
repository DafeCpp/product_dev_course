"""Token buckets shared by all telemetry-ingest replicas through Redis.

Each sensor/transport pair is one Redis hash holding the used tokens and the
last update time of its two buckets. A Lua script drains, checks and takes
both buckets in one atomic step, using the same arithmetic as
:class:`TokenBucket`, so every replica sees one limit and the counters
survive a service restart. Time comes from the Redis server (``TIME``), so
replica clocks don't need to agree.

When Redis fails (connection refused, timeout, script error) :meth:`reserve`
returns ``None`` and the caller falls back to its in-memory buckets. Redis is
not tried again for ``retry_seconds``, so an outage costs one short timeout
per cooldown instead of one per request.
"""
from __future__ import annotations

import asyncio
import logging
import math
import time
from collections.abc import Callable
from typing import Any

from redis.asyncio import Redis
from redis.backoff import NoBackoff
from redis.exceptions import RedisError
from redis.retry import Retry

logger = logging.getLogger(__name__)

# KEYS[1]: bucket hash. ARGV: cost_a, cap_a, cost_b, cap_b, window, ttl.
# Returns {wait_a, wait_b}; -1 means the bucket has room. Tokens are taken
# only when both buckets have room. A capacity of 0 means unlimited.
_RESERVE_LUA = """
local t = redis.call('TIME')
local now = tonumber(t[1]) + tonumber(t[2]) / 1000000
local window = tonumber(ARGV[5])
local unpack = unpack or table.unpack
local state = redis.call('HMGET', KEYS[1], 'ua', 'ta', 'ub', 'tb')

local function wait_for(used, last, cost, cap)
  used = tonumber(used) or 0
  if cap == 0 then
    return used, last, -1
  end
  last = tonumber(last)
  if last then
    used = math.max(0, used - math.max(0, now - last) * cap / window)
  end
  used = math.min(used, cap)
  if used + cost <= cap then
    return used, now, -1
  end
  if cost > cap then
    return used, now, math.max(1, math.ceil(window))
  end
  return used, now, math.max(1, math.ceil((used + cost - cap) * window / cap))
end

local cost_a, cap_a = tonumber(ARGV[1]), tonumber(ARGV[2])
local cost_b, cap_b = tonumber(ARGV[3]), tonumber(ARGV[4])
local ua, ta, wait_a = wait_for(state[1], state[2], cost_a, cap_a)
local ub, tb, wait_b = wait_for(state[3], state[4], cost_b, cap_b)
if wait_a < 0 and wait_b < 0 then
  if cap_a > 0 then ua = ua + cost_a end
  if cap_b > 0 then ub = ub + cost_b end
end

local fields = {'ua', tostring(ua), 'ub', tostring(ub)}
if ta then
  table.insert(fields, 'ta')
  table.insert(fields, tostring(ta))
end
if tb then
  table.insert(fields, 'tb')
  table.insert(fields, tostring(tb))
end
redis.call('HSET', KEYS[1], unpack(fields))
redis.call('EXPIRE', KEYS[1], tonumber(ARGV[6]))
return {wait_a, wait_b}
"""

Waits = tuple[int | None, int | None]


class RedisBuckets:
    """Atomic two-bucket reservations in Redis, with a cooldown after failures.

    Args:
        client: async Redis client; use a short socket timeout so a stalled
            Redis delays a request by at most that long.
        prefix: key prefix shared by all replicas.
        retry_seconds: how long to stay on the in-memory fallback after a
            failure before trying Redis again.
    """

    def __init__(
        self,
        client: Redis,
        *,
        prefix: str = "telemetry-ingest:rate-limit",
        retry_seconds: float = 5.0,
        clock: Callable[[], float] = time.monotonic,
    ) -> None:
        self._client = client
        self._script = client.register_script(_RESERVE_LUA)
        self._prefix = prefix
        self._retry_seconds = retry_seconds
        self._clock = clock
        self._down_until: float | None = None

    async def reserve(
        self,
        key: str,
        first: tuple[int, int],
        second: tuple[int, int],
        window_seconds: float,
    ) -> Waits | None:
        """Take ``first`` and ``second`` (``(cost, capacity)``) if both fit.

        Returns the wait of each bucket (``None`` = has room), or ``None``
        when Redis is unavailable and the caller should use its fallback.
        """
        now = self._clock()
        if self._down_until is not None and now < self._down_until:
            return None
        # A bucket fully drains within one window; the margin covers a
        # window raised at runtime before the key is touched again.
        ttl = max(1, math.ceil(window_seconds)) * 2 + 1
        try:
            reply: Any = await self._script(
                keys=[f"{self._prefix}:{key}"],
                args=[first[0], first[1], second[0], second[1], window_seconds, ttl],
            )
            waits = (int(reply[0]), int(reply[1]))
        except (RedisError, OSError, asyncio.TimeoutError, TypeError, ValueError, IndexError) as exc:
            if self._down_until is None:
                logger.warning(
                    "Rate-limit Redis unavailable, using in-memory limits for %.0fs: %s",
                    self._retry_seconds,
                    exc,
                )
            self._down_until = now + self._retry_seconds
            return None
        if self._down_until is not None:
            logger.info("Rate-limit Redis is back, using shared limits")
            self._down_until = None
        return (None if waits[0] < 0 else waits[0], None if waits[1] < 0 else waits[1])

    async def close(self) -> None:
        await self._client.aclose()


def build_redis_buckets(url: str, *, timeout_seconds: float, retry_seconds: float) -> RedisBuckets:
    client = Redis.from_url(
        url,
        socket_timeout=timeout_seconds,
        socket_connect_timeout=timeout_seconds,
        # No client-side retries: a failed call falls back at once.
        retry=Retry(NoBackoff(), 0),
    )
    return RedisBuckets(client, retry_seconds=retry_seconds)
