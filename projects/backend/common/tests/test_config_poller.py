"""Unit tests for config_client.poller (make_config_subscriber + build_config_client)."""
from __future__ import annotations

from dataclasses import dataclass

from pydantic import BaseModel

from backend_common.config_client import build_config_client, make_config_subscriber

CONFIG_KEY = "test_qos"


@dataclass
class _FlatTarget:
    timeout_sec: float = 5.0
    max_requests: int = 100


class _FlatValue(BaseModel, extra="ignore"):
    timeout_sec: float | None = None
    max_requests: int | None = None


@dataclass
class _NestedTarget:
    rest_max_requests: int = 600
    rest_window_seconds: float = 60.0
    ws_max_messages: int = 600
    spool_timeout: float = 5.0


class _RestValue(BaseModel, extra="ignore"):
    max_requests: int | None = None
    window_seconds: float | None = None


class _WsValue(BaseModel, extra="ignore"):
    max_messages: int | None = None


class _NestedValue(BaseModel, extra="ignore"):
    rest: _RestValue = _RestValue()
    ws: _WsValue = _WsValue()
    spool_timeout: float | None = None


# ---------------------------------------------------------------------------
# Flat model
# ---------------------------------------------------------------------------


def test_applies_all_fields():
    target = _FlatTarget()
    apply = make_config_subscriber(CONFIG_KEY, _FlatValue, target)
    apply({CONFIG_KEY: {"timeout_sec": 30.0, "max_requests": 500}})
    assert target.timeout_sec == 30.0
    assert target.max_requests == 500


def test_partial_update_keeps_other_fields():
    target = _FlatTarget()
    apply = make_config_subscriber(CONFIG_KEY, _FlatValue, target)
    apply({CONFIG_KEY: {"max_requests": 42}})
    assert target.max_requests == 42
    assert target.timeout_sec == 5.0


def test_empty_payload_is_no_op():
    target = _FlatTarget()
    apply = make_config_subscriber(CONFIG_KEY, _FlatValue, target)
    apply({CONFIG_KEY: {}})
    assert target == _FlatTarget()


def test_missing_key_is_no_op():
    target = _FlatTarget()
    apply = make_config_subscriber(CONFIG_KEY, _FlatValue, target)
    apply({"other_key": {"timeout_sec": 1.0}})
    assert target == _FlatTarget()


def test_invalid_payload_fails_open():
    target = _FlatTarget()
    apply = make_config_subscriber(CONFIG_KEY, _FlatValue, target)
    apply({CONFIG_KEY: {"max_requests": "not_an_int"}})
    assert target == _FlatTarget()


def test_non_object_payload_fails_open():
    target = _FlatTarget()
    apply = make_config_subscriber(CONFIG_KEY, _FlatValue, target)
    apply({CONFIG_KEY: "garbage"})
    assert target == _FlatTarget()


def test_extra_fields_are_ignored():
    target = _FlatTarget()
    apply = make_config_subscriber(CONFIG_KEY, _FlatValue, target)
    apply({CONFIG_KEY: {"max_requests": 7, "unknown_knob": 1}})
    assert target.max_requests == 7


# ---------------------------------------------------------------------------
# Nested model — underscore-prefixed flattening
# ---------------------------------------------------------------------------


def test_nested_fields_map_to_prefixed_attributes():
    target = _NestedTarget()
    apply = make_config_subscriber(CONFIG_KEY, _NestedValue, target)
    apply({CONFIG_KEY: {
        "rest": {"max_requests": 1000, "window_seconds": 120.0},
        "ws": {"max_messages": 300},
        "spool_timeout": 10.0,
    }})
    assert target.rest_max_requests == 1000
    assert target.rest_window_seconds == 120.0
    assert target.ws_max_messages == 300
    assert target.spool_timeout == 10.0


def test_nested_partial_update():
    target = _NestedTarget()
    apply = make_config_subscriber(CONFIG_KEY, _NestedValue, target)
    apply({CONFIG_KEY: {"rest": {"max_requests": 5}}})
    assert target.rest_max_requests == 5
    assert target.rest_window_seconds == 60.0
    assert target.ws_max_messages == 600


# ---------------------------------------------------------------------------
# Model field without a matching target attribute
# ---------------------------------------------------------------------------


def test_unknown_target_attribute_is_skipped_others_applied():
    class _Mismatched(BaseModel, extra="ignore"):
        timeout_sec: float | None = None
        no_such_attr: int | None = None

    target = _FlatTarget()
    apply = make_config_subscriber(CONFIG_KEY, _Mismatched, target)
    apply({CONFIG_KEY: {"timeout_sec": 9.0, "no_such_attr": 1}})
    assert target.timeout_sec == 9.0
    assert not hasattr(target, "no_such_attr")


# ---------------------------------------------------------------------------
# build_config_client
# ---------------------------------------------------------------------------


def test_build_config_client_attaches_subscribers():
    target = _FlatTarget()
    apply = make_config_subscriber(CONFIG_KEY, _FlatValue, target)
    client = build_config_client("test-service", "http://cfg:8005", 2.0, apply)
    assert client._subscribers == [apply]
    assert client.service_name == "test-service"
    assert client._poll_interval == 2.0
