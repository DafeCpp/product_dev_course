"""Config-service poller for telemetry-ingest rate-limit & timeout configuration.

Subscribes to ConfigClient bulk updates and mutates the shared
``RATE_LIMIT_CONFIG`` singleton in-place whenever the ``rate_limits`` key
changes.  Both REST and WS limiters read fields from that singleton on every
``check()`` call, so new limits take effect on the next inbound request —
without restarting the service and without discarding per-sensor window state.

Expected value shape for key ``"rate_limits"`` in config-service
(config_type ``telemetry_rate_limit``)::

    {
        "rest": {"max_requests": 600, "max_readings": 60000, "window_seconds": 60.0},
        "ws":   {"max_messages": 600, "max_readings": 60000, "window_seconds": 1.0},
        "spool_flush_timeout_seconds": 5.0,
        "ws_max_message_bytes": 1048576
    }

Any field may be omitted; missing fields keep their current value.
Setting a limit to 0 means *unlimited* for that counter (see RateLimitConfig).
"""
from __future__ import annotations

from typing import Any

import structlog
from pydantic import BaseModel, ValidationError

from backend_common.config_client import ConfigClient
from telemetry_ingest_service.middleware.rate_limit_config import RATE_LIMIT_CONFIG

logger = structlog.get_logger(__name__)

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


def _apply_config(configs: dict[str, Any]) -> None:
    """Subscriber callback: apply rate_limits from config-service bulk payload."""
    raw = configs.get(CONFIG_KEY)
    if raw is None:
        return

    try:
        value = _RateLimitsValue.model_validate(raw)
    except ValidationError:
        logger.warning("config_poller invalid rate_limits payload", raw=raw)
        return

    # Atomic in-place mutation — asyncio is single-threaded, no await between
    # reads and writes, so this is safe without a lock.
    rest = value.rest
    ws = value.ws
    if rest.max_requests is not None:
        RATE_LIMIT_CONFIG.rest_max_requests = rest.max_requests
    if rest.max_readings is not None:
        RATE_LIMIT_CONFIG.rest_max_readings = rest.max_readings
    if rest.window_seconds is not None:
        RATE_LIMIT_CONFIG.rest_window_seconds = rest.window_seconds
    if ws.max_messages is not None:
        RATE_LIMIT_CONFIG.ws_max_messages = ws.max_messages
    if ws.max_readings is not None:
        RATE_LIMIT_CONFIG.ws_max_readings = ws.max_readings
    if ws.window_seconds is not None:
        RATE_LIMIT_CONFIG.ws_window_seconds = ws.window_seconds
    if value.spool_flush_timeout_seconds is not None:
        RATE_LIMIT_CONFIG.spool_flush_timeout_seconds = value.spool_flush_timeout_seconds
    if value.ws_max_message_bytes is not None:
        RATE_LIMIT_CONFIG.ws_max_message_bytes = value.ws_max_message_bytes

    logger.info(
        "config_poller applied rate_limits",
        rest_max_requests=RATE_LIMIT_CONFIG.rest_max_requests,
        rest_max_readings=RATE_LIMIT_CONFIG.rest_max_readings,
        rest_window_seconds=RATE_LIMIT_CONFIG.rest_window_seconds,
        ws_max_messages=RATE_LIMIT_CONFIG.ws_max_messages,
        ws_max_readings=RATE_LIMIT_CONFIG.ws_max_readings,
        ws_window_seconds=RATE_LIMIT_CONFIG.ws_window_seconds,
        spool_flush_timeout_seconds=RATE_LIMIT_CONFIG.spool_flush_timeout_seconds,
        ws_max_message_bytes=RATE_LIMIT_CONFIG.ws_max_message_bytes,
    )


def build_config_client(url: str, poll_interval: float) -> ConfigClient:
    """Build and wire up a ConfigClient for telemetry-ingest."""
    client = ConfigClient(
        "telemetry-ingest",
        url,
        poll_interval=poll_interval,
    )
    client.subscribe(_apply_config)
    return client
