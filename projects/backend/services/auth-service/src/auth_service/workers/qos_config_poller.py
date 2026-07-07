"""QoS poller for auth-service configuration from config-service.

Subscribes to ConfigClient bulk updates and mutates the shared
``QOS_CONFIG`` singleton in-place whenever the ``auth_qos`` key changes.
JWT token creation reads fields from that singleton on every token issue,
so new TTLs take effect on the next login/refresh — without restarting
the service.

Expected value shape for key ``"auth_qos"`` in config-service
(config_type ``qos``)::

    {
        "access_token_ttl_sec": 900,
        "refresh_token_ttl_sec": 1209600
    }

Any field may be omitted; missing fields keep their current value.
"""
from __future__ import annotations

from typing import Any

import structlog
from pydantic import BaseModel, ValidationError

from backend_common.config_client import ConfigClient
from auth_service.middleware.qos_config import QOS_CONFIG

logger = structlog.get_logger(__name__)

CONFIG_KEY = "auth_qos"


class _AuthQosValue(BaseModel, extra="ignore"):
    access_token_ttl_sec: int | None = None
    refresh_token_ttl_sec: int | None = None


def _apply_qos_config(configs: dict[str, Any]) -> None:
    """Subscriber callback: apply auth_qos from config-service bulk payload."""
    raw = configs.get(CONFIG_KEY)
    if raw is None:
        return

    try:
        value = _AuthQosValue.model_validate(raw)
    except ValidationError:
        logger.warning("qos_config_poller invalid auth_qos payload", raw=raw)
        return

    # Atomic in-place mutation — asyncio is single-threaded, no await between
    # reads and writes, so this is safe without a lock.
    if value.access_token_ttl_sec is not None:
        QOS_CONFIG.access_token_ttl_sec = value.access_token_ttl_sec
    if value.refresh_token_ttl_sec is not None:
        QOS_CONFIG.refresh_token_ttl_sec = value.refresh_token_ttl_sec

    logger.info(
        "qos_config_poller applied auth_qos",
        access_token_ttl_sec=QOS_CONFIG.access_token_ttl_sec,
        refresh_token_ttl_sec=QOS_CONFIG.refresh_token_ttl_sec,
    )


def build_qos_client(url: str, poll_interval: float) -> ConfigClient:
    """Build and wire up a ConfigClient for auth-service QoS."""
    client = ConfigClient(
        "auth-service",
        url,
        poll_interval=poll_interval,
    )
    client.subscribe(_apply_qos_config)
    return client
