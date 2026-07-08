"""Config-service poller for telemetry-ingest rate-limit & timeout configuration.

Subscribes to ConfigClient bulk updates and mutates the shared
``RATE_LIMIT_CONFIG`` singleton in-place whenever the ``rate_limits`` key
changes.  Both REST and WS limiters read fields from that singleton on every
``check()`` call, so new limits take effect on the next inbound request —
without restarting the service and without discarding per-sensor window state.

Expected value shape for key ``"rate_limits"`` in config-service
(config_type ``qos``)::

    {
        "rest": {"max_requests": 600, "max_readings": 60000, "window_seconds": 60.0},
        "ws":   {"max_messages": 600, "max_readings": 60000, "window_seconds": 1.0},
        "spool_flush_timeout_seconds": 5.0,
        "ws_max_message_bytes": 1048576
    }

Any field may be omitted; missing fields keep their current value.
Setting a limit to 0 means *unlimited* for that counter (see RateLimitConfig).
Nested ``rest``/``ws`` fields map to prefixed singleton attributes
(``rest.max_requests`` → ``rest_max_requests``); subscriber semantics
(fail-open, partial updates) live in ``backend_common.config_client.poller``.
"""
from __future__ import annotations

from pydantic import BaseModel

from backend_common.config_client import ConfigClient, make_config_subscriber
from backend_common.config_client import build_config_client as _build_client
from telemetry_ingest_service.middleware.rate_limit_config import RATE_LIMIT_CONFIG

CONFIG_KEY = "rate_limits"


class _RestLimits(BaseModel, extra="ignore"):
    max_requests: int | None = None
    max_readings: int | None = None
    window_seconds: float | None = None


class _WsLimits(BaseModel, extra="ignore"):
    max_messages: int | None = None
    max_readings: int | None = None
    window_seconds: float | None = None


class _RateLimitsValue(BaseModel, extra="ignore"):
    rest: _RestLimits = _RestLimits()
    ws: _WsLimits = _WsLimits()
    spool_flush_timeout_seconds: float | None = None
    ws_max_message_bytes: int | None = None


_apply_config = make_config_subscriber(CONFIG_KEY, _RateLimitsValue, RATE_LIMIT_CONFIG)


def build_config_client(url: str, poll_interval: float) -> ConfigClient:
    """Build and wire up a ConfigClient for telemetry-ingest."""
    return _build_client("telemetry-ingest", url, poll_interval, _apply_config)
