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
Subscriber semantics (fail-open, partial updates) live in
``backend_common.config_client.poller``.
"""
from __future__ import annotations

from pydantic import BaseModel

from backend_common.config_client import ConfigClient, build_config_client, make_config_subscriber
from experiment_service.middleware.qos_config import QOS_CONFIG

CONFIG_KEY = "experiment_qos"


class _ExperimentQosValue(BaseModel, extra="ignore"):
    rate_limit_max_requests: int | None = None
    downstream_timeout_seconds: float | None = None


_apply_qos_config = make_config_subscriber(CONFIG_KEY, _ExperimentQosValue, QOS_CONFIG)


def build_qos_client(url: str, poll_interval: float) -> ConfigClient:
    """Build and wire up a ConfigClient for experiment-service QoS."""
    return build_config_client("experiment-service", url, poll_interval, _apply_qos_config)
