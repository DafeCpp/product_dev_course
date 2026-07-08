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
Subscriber semantics (fail-open, partial updates) live in
``backend_common.config_client.poller``.
"""
from __future__ import annotations

from pydantic import BaseModel

from backend_common.config_client import ConfigClient, build_config_client, make_config_subscriber
from auth_service.middleware.qos_config import QOS_CONFIG

CONFIG_KEY = "auth_qos"


class _AuthQosValue(BaseModel, extra="ignore"):
    access_token_ttl_sec: int | None = None
    refresh_token_ttl_sec: int | None = None


_apply_qos_config = make_config_subscriber(CONFIG_KEY, _AuthQosValue, QOS_CONFIG)


def build_qos_client(url: str, poll_interval: float) -> ConfigClient:
    """Build and wire up a ConfigClient for auth-service QoS."""
    return build_config_client("auth-service", url, poll_interval, _apply_qos_config)
