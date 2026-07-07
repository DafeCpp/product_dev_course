"""QoS poller for experiment-service configuration from config-service.

Subscribes to ConfigClient bulk updates and mutates the shared
``QOS_CONFIG`` singleton in-place whenever the ``experiment_qos`` key changes.

Expected value shape for key ``"experiment_qos"`` in config-service
(config_type ``qos``)::

    {
        "rate_limit_max_requests": 1000,
        "downstream_timeout_seconds": 30.0
    }

Any field may be omitted; missing fields keep their current value.
"""
from __future__ import annotations

from typing import Any

import structlog
from pydantic import BaseModel, ValidationError

from backend_common.config_client import ConfigClient
from experiment_service.middleware.qos_config import QOS_CONFIG

logger = structlog.get_logger(__name__)

CONFIG_KEY = "experiment_qos"


class _ExperimentQosValue(BaseModel, extra="ignore"):
    rate_limit_max_requests: int | None = None
    downstream_timeout_seconds: float | None = None


def _apply_qos_config(configs: dict[str, Any]) -> None:
    """Subscriber callback: apply experiment_qos from config-service bulk payload."""
    raw = configs.get(CONFIG_KEY)
    if raw is None:
        return

    try:
        value = _ExperimentQosValue.model_validate(raw)
    except ValidationError:
        logger.warning("qos_config_poller invalid experiment_qos payload", raw=raw)
        return

    # Atomic in-place mutation — asyncio is single-threaded, no await between
    # reads and writes, so this is safe without a lock.
    if value.rate_limit_max_requests is not None:
        QOS_CONFIG.rate_limit_max_requests = value.rate_limit_max_requests
    if value.downstream_timeout_seconds is not None:
        QOS_CONFIG.downstream_timeout_seconds = value.downstream_timeout_seconds

    logger.info(
        "qos_config_poller applied experiment_qos",
        rate_limit_max_requests=QOS_CONFIG.rate_limit_max_requests,
        downstream_timeout_seconds=QOS_CONFIG.downstream_timeout_seconds,
    )


def build_qos_client(url: str, poll_interval: float) -> ConfigClient:
    """Build and wire up a ConfigClient for experiment-service QoS."""
    client = ConfigClient(
        "experiment-service",
        url,
        poll_interval=poll_interval,
    )
    client.subscribe(_apply_qos_config)
    return client
