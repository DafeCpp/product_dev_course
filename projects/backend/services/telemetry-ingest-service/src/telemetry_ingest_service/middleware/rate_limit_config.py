"""Shared, mutable rate-limit configuration for REST and WebSocket ingest.

Both ``IngestRateLimiter`` and ``WsRateLimiter`` hold a reference to a single
``RateLimitConfig`` instance and read its fields on every ``check()`` call.
This lets an operator change limits at runtime (via the admin endpoint) and
have the new values take effect on the next request — without restarting the
service and without discarding the per-sensor window state.

For every limit, a value of ``0`` means *unlimited* (that counter is not
enforced).  Each counter is independent, so e.g. ``rest_max_requests=0`` with a
finite ``rest_max_readings`` still enforces the readings cap.

Thread safety: asyncio is single-threaded; field updates happen in a single
synchronous block without ``await``, so mutation is atomic by construction.
"""
from __future__ import annotations

from dataclasses import dataclass

from telemetry_ingest_service.settings import Settings, settings as _settings


@dataclass
class RateLimitConfig:
    """Mutable limits for both ingest transports (fixed-window) + spool timeouts."""

    rest_max_requests: int
    rest_max_readings: int
    rest_window_seconds: float
    ws_max_messages: int
    ws_max_readings: int
    ws_window_seconds: float
    spool_flush_timeout_seconds: float
    ws_max_message_bytes: int

    @classmethod
    def from_settings(cls, s: Settings) -> "RateLimitConfig":
        return cls(
            rest_max_requests=s.rest_rate_limit_requests_per_window,
            rest_max_readings=s.rest_rate_limit_readings_per_window,
            rest_window_seconds=s.rest_rate_limit_window_seconds,
            ws_max_messages=s.ws_rate_limit_messages_per_window,
            ws_max_readings=s.ws_rate_limit_readings_per_window,
            ws_window_seconds=s.ws_rate_limit_window_seconds,
            spool_flush_timeout_seconds=s.spool_flush_interval_seconds,
            ws_max_message_bytes=s.ws_max_message_bytes,
        )


# Single shared instance referenced by both limiters and the admin endpoint.
RATE_LIMIT_CONFIG = RateLimitConfig.from_settings(_settings)
