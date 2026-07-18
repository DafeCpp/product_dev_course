"""Generic config-service subscriber for mutable runtime-config singletons.

Backend services share one pattern for dynamic QoS/rate-limit configuration
(LOS-61/62/63): a mutable dataclass singleton holds the current values, hot
paths read it on every call, and a ConfigClient subscriber mutates it in place
whenever the service's key changes in config-service.  This module extracts
the subscriber boilerplate; a service keeps only its Pydantic value model and
the singleton.

Usage::

    class _AuthQosValue(BaseModel, extra="ignore"):
        access_token_ttl_sec: int | None = None
        refresh_token_ttl_sec: int | None = None

    _apply_qos_config = make_config_subscriber("auth_qos", _AuthQosValue, QOS_CONFIG)

    def build_qos_client(url: str, poll_interval: float) -> ConfigClient:
        return build_config_client("auth-service", url, poll_interval, _apply_qos_config)

Semantics (identical for every service):

- the key is read from the bulk payload; an absent key is a no-op;
- fail-open: a payload that fails model validation is logged and ignored,
  current values stay in effect;
- only non-``None`` fields are applied — a partial payload means "keep the
  rest as is";
- nested models map to underscore-prefixed target attributes
  (``rest.max_requests`` → ``rest_max_requests``).
"""
from __future__ import annotations

from typing import Any, Callable

import structlog
from pydantic import BaseModel, ValidationError

from backend_common.config_client.client import ConfigClient

logger = structlog.get_logger(__name__)


def _flatten(data: dict[str, Any], prefix: str = "") -> dict[str, Any]:
    flat: dict[str, Any] = {}
    for key, value in data.items():
        name = f"{prefix}{key}"
        if isinstance(value, dict):
            flat.update(_flatten(value, prefix=f"{name}_"))
        else:
            flat[name] = value
    return flat


def make_config_subscriber(
    config_key: str,
    model: type[BaseModel],
    target: object,
) -> Callable[[dict[str, Any]], None]:
    """Build a ConfigClient subscriber that applies *config_key* to *target*.

    The returned callback validates the raw value via *model* and copies every
    non-``None`` (flattened) field onto the same-named attribute of *target*.
    Mutation is atomic without a lock: asyncio is single-threaded and there is
    no await between reads and writes.
    """

    def _apply(configs: dict[str, Any]) -> None:
        raw = configs.get(config_key)
        if raw is None:
            return

        try:
            value = model.model_validate(raw)
        except ValidationError:
            logger.warning("config_poller invalid payload", config_key=config_key, raw=raw)
            return

        applied: dict[str, Any] = {}
        for attr, val in _flatten(value.model_dump(exclude_none=True)).items():
            if not hasattr(target, attr):
                # Model field without a matching singleton attribute — a bug
                # in the service wiring, not in the payload; skip loudly.
                logger.error(
                    "config_poller unknown target attribute",
                    config_key=config_key,
                    attribute=attr,
                )
                continue
            setattr(target, attr, val)
            applied[attr] = val

        if applied:
            logger.info("config_poller applied", config_key=config_key, **applied)

    return _apply


def build_config_client(
    service_name: str,
    url: str,
    poll_interval: float,
    *subscribers: Callable[[dict[str, Any]], Any],
) -> ConfigClient:
    """Build a ConfigClient and attach *subscribers* to it."""
    client = ConfigClient(service_name, url, poll_interval=poll_interval)
    for callback in subscribers:
        client.subscribe(callback)
    return client
