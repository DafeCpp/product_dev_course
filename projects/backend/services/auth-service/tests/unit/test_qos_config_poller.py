"""Unit tests for workers.qos_config_poller (_apply_qos_config + build_qos_client)."""
from __future__ import annotations

from unittest.mock import patch

import pytest

from auth_service.middleware.qos_config import QOS_CONFIG
from auth_service.workers.qos_config_poller import CONFIG_KEY, _apply_qos_config, build_qos_client


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _snapshot() -> dict:
    return {
        "access_token_ttl_sec": QOS_CONFIG.access_token_ttl_sec,
        "refresh_token_ttl_sec": QOS_CONFIG.refresh_token_ttl_sec,
    }


def _restore(snap: dict) -> None:
    for k, v in snap.items():
        setattr(QOS_CONFIG, k, v)


@pytest.fixture(autouse=True)
def restore_qos_config():
    """Restore QOS_CONFIG after each test (singleton is global)."""
    snap = _snapshot()
    yield
    _restore(snap)


# ---------------------------------------------------------------------------
# _apply_qos_config — happy path
# ---------------------------------------------------------------------------


def test_apply_qos_config_updates_access_token_ttl():
    _apply_qos_config({CONFIG_KEY: {"access_token_ttl_sec": 1800}})
    assert QOS_CONFIG.access_token_ttl_sec == 1800


def test_apply_qos_config_updates_refresh_token_ttl():
    _apply_qos_config({CONFIG_KEY: {"refresh_token_ttl_sec": 2592000}})
    assert QOS_CONFIG.refresh_token_ttl_sec == 2592000


def test_apply_qos_config_updates_both_fields():
    _apply_qos_config({CONFIG_KEY: {
        "access_token_ttl_sec": 600,
        "refresh_token_ttl_sec": 604800,
    }})
    assert QOS_CONFIG.access_token_ttl_sec == 600
    assert QOS_CONFIG.refresh_token_ttl_sec == 604800


# ---------------------------------------------------------------------------
# _apply_qos_config — partial updates (missing fields keep current value)
# ---------------------------------------------------------------------------


def test_apply_qos_config_partial_update_keeps_other_fields():
    original_refresh_ttl = QOS_CONFIG.refresh_token_ttl_sec
    _apply_qos_config({CONFIG_KEY: {"access_token_ttl_sec": 999}})
    assert QOS_CONFIG.access_token_ttl_sec == 999
    assert QOS_CONFIG.refresh_token_ttl_sec == original_refresh_ttl


def test_apply_qos_config_empty_payload_is_no_op():
    snap = _snapshot()
    _apply_qos_config({CONFIG_KEY: {}})
    assert _snapshot() == snap


# ---------------------------------------------------------------------------
# _apply_qos_config — missing key is no-op
# ---------------------------------------------------------------------------


def test_apply_qos_config_missing_auth_qos_key_is_no_op():
    snap = _snapshot()
    _apply_qos_config({"other_key": {"access_token_ttl_sec": 1}})
    assert _snapshot() == snap


def test_apply_qos_config_empty_configs_dict_is_no_op():
    snap = _snapshot()
    _apply_qos_config({})
    assert _snapshot() == snap


# ---------------------------------------------------------------------------
# _apply_qos_config — invalid payload is ignored
# ---------------------------------------------------------------------------


def test_apply_qos_config_invalid_type_string_is_ignored():
    snap = _snapshot()
    _apply_qos_config({CONFIG_KEY: "not a dict"})
    assert _snapshot() == snap


def test_apply_qos_config_invalid_ttl_type_is_ignored():
    snap = _snapshot()
    # access_token_ttl_sec must be int — string fails Pydantic coercion
    _apply_qos_config({CONFIG_KEY: {"access_token_ttl_sec": "not_a_number"}})
    assert _snapshot() == snap


def test_apply_qos_config_extra_fields_are_ignored():
    _apply_qos_config({CONFIG_KEY: {
        "access_token_ttl_sec": 42,
        "unknown_field": "ignored",
    }})
    assert QOS_CONFIG.access_token_ttl_sec == 42


# ---------------------------------------------------------------------------
# build_qos_client
# ---------------------------------------------------------------------------


def test_build_qos_client_returns_client_with_subscriber():
    client = build_qos_client("http://cfg:8005", poll_interval=1.0)
    assert len(client._subscribers) == 1
    assert client._subscribers[0] is _apply_qos_config


def test_build_qos_client_service_name():
    client = build_qos_client("http://cfg:8005", poll_interval=1.0)
    assert client.service_name == "auth-service"


def test_build_qos_client_url():
    client = build_qos_client("http://cfg:8005", poll_interval=2.0)
    assert "cfg:8005" in client._url
    assert client._poll_interval == 2.0


# ---------------------------------------------------------------------------
# Integration: subscriber fires and mutates config
# ---------------------------------------------------------------------------


def test_subscriber_wired_to_apply_qos_config():
    """Calling subscriber[0] directly with a configs dict applies the config."""
    client = build_qos_client("http://cfg:8005", poll_interval=1.0)
    cb = client._subscribers[0]

    cb({CONFIG_KEY: {"access_token_ttl_sec": 777}})
    assert QOS_CONFIG.access_token_ttl_sec == 777
