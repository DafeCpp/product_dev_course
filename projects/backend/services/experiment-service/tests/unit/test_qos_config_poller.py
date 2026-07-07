"""Unit tests for workers.qos_config_poller (_apply_qos_config + build_qos_client)."""
from __future__ import annotations

import pytest

from experiment_service.middleware.qos_config import QOS_CONFIG
from experiment_service.workers.qos_config_poller import CONFIG_KEY, _apply_qos_config, build_qos_client


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _snapshot() -> dict:
    return {
        "rate_limit_max_requests": QOS_CONFIG.rate_limit_max_requests,
        "downstream_timeout_seconds": QOS_CONFIG.downstream_timeout_seconds,
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


def test_apply_qos_config_updates_rate_limit():
    _apply_qos_config({CONFIG_KEY: {"rate_limit_max_requests": 500}})
    assert QOS_CONFIG.rate_limit_max_requests == 500


def test_apply_qos_config_updates_downstream_timeout():
    _apply_qos_config({CONFIG_KEY: {"downstream_timeout_seconds": 45.0}})
    assert QOS_CONFIG.downstream_timeout_seconds == 45.0


def test_apply_qos_config_updates_both_fields():
    _apply_qos_config({CONFIG_KEY: {
        "rate_limit_max_requests": 2000,
        "downstream_timeout_seconds": 60.0,
    }})
    assert QOS_CONFIG.rate_limit_max_requests == 2000
    assert QOS_CONFIG.downstream_timeout_seconds == 60.0


# ---------------------------------------------------------------------------
# _apply_qos_config — partial updates (missing fields keep current value)
# ---------------------------------------------------------------------------


def test_apply_qos_config_partial_update_keeps_other_fields():
    original_timeout = QOS_CONFIG.downstream_timeout_seconds
    _apply_qos_config({CONFIG_KEY: {"rate_limit_max_requests": 999}})
    assert QOS_CONFIG.rate_limit_max_requests == 999
    assert QOS_CONFIG.downstream_timeout_seconds == original_timeout


def test_apply_qos_config_empty_payload_is_no_op():
    snap = _snapshot()
    _apply_qos_config({CONFIG_KEY: {}})
    assert _snapshot() == snap


# ---------------------------------------------------------------------------
# _apply_qos_config — missing key is no-op
# ---------------------------------------------------------------------------


def test_apply_qos_config_missing_experiment_qos_key_is_no_op():
    snap = _snapshot()
    _apply_qos_config({"other_key": {"rate_limit_max_requests": 1}})
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


def test_apply_qos_config_invalid_rate_limit_type_is_ignored():
    snap = _snapshot()
    # rate_limit_max_requests must be int — string fails Pydantic coercion
    _apply_qos_config({CONFIG_KEY: {"rate_limit_max_requests": "not_a_number"}})
    assert _snapshot() == snap


def test_apply_qos_config_extra_fields_are_ignored():
    _apply_qos_config({CONFIG_KEY: {
        "rate_limit_max_requests": 42,
        "unknown_field": "ignored",
    }})
    assert QOS_CONFIG.rate_limit_max_requests == 42


# ---------------------------------------------------------------------------
# build_qos_client
# ---------------------------------------------------------------------------


def test_build_qos_client_returns_client_with_subscriber():
    client = build_qos_client("http://cfg:8005", poll_interval=1.0)
    assert len(client._subscribers) == 1
    assert client._subscribers[0] is _apply_qos_config


def test_build_qos_client_service_name():
    client = build_qos_client("http://cfg:8005", poll_interval=1.0)
    assert client.service_name == "experiment-service"


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

    cb({CONFIG_KEY: {"rate_limit_max_requests": 777}})
    assert QOS_CONFIG.rate_limit_max_requests == 777
