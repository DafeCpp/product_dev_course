"""Unit tests for workers.config_poller (_apply_config + build_config_client)."""
from __future__ import annotations

from unittest.mock import MagicMock, patch

import pytest

from telemetry_ingest_service.middleware.rate_limit_config import RATE_LIMIT_CONFIG, RateLimitConfig
from telemetry_ingest_service.workers.config_poller import CONFIG_KEY, _apply_config, build_config_client


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _snapshot() -> dict:
    return {
        "rest_max_requests": RATE_LIMIT_CONFIG.rest_max_requests,
        "rest_max_readings": RATE_LIMIT_CONFIG.rest_max_readings,
        "rest_window_seconds": RATE_LIMIT_CONFIG.rest_window_seconds,
        "ws_max_messages": RATE_LIMIT_CONFIG.ws_max_messages,
        "ws_max_readings": RATE_LIMIT_CONFIG.ws_max_readings,
        "ws_window_seconds": RATE_LIMIT_CONFIG.ws_window_seconds,
    }


def _restore(snap: dict) -> None:
    for k, v in snap.items():
        setattr(RATE_LIMIT_CONFIG, k, v)


@pytest.fixture(autouse=True)
def restore_rate_limit_config():
    """Restore RATE_LIMIT_CONFIG after each test (singleton is global)."""
    snap = _snapshot()
    yield
    _restore(snap)


# ---------------------------------------------------------------------------
# _apply_config — happy path
# ---------------------------------------------------------------------------


def test_apply_config_updates_rest_fields():
    _apply_config({CONFIG_KEY: {"rest": {"max_requests": 100, "max_readings": 5000, "window_seconds": 30.0}}})
    assert RATE_LIMIT_CONFIG.rest_max_requests == 100
    assert RATE_LIMIT_CONFIG.rest_max_readings == 5000
    assert RATE_LIMIT_CONFIG.rest_window_seconds == 30.0


def test_apply_config_updates_ws_fields():
    _apply_config({CONFIG_KEY: {"ws": {"max_messages": 200, "max_readings": 10000, "window_seconds": 2.0}}})
    assert RATE_LIMIT_CONFIG.ws_max_messages == 200
    assert RATE_LIMIT_CONFIG.ws_max_readings == 10000
    assert RATE_LIMIT_CONFIG.ws_window_seconds == 2.0


def test_apply_config_updates_both_transports():
    _apply_config({CONFIG_KEY: {
        "rest": {"max_requests": 1, "max_readings": 1, "window_seconds": 1.0},
        "ws":   {"max_messages": 2, "max_readings": 2, "window_seconds": 2.0},
    }})
    assert RATE_LIMIT_CONFIG.rest_max_requests == 1
    assert RATE_LIMIT_CONFIG.ws_max_messages == 2


def test_apply_config_zero_means_unlimited():
    _apply_config({CONFIG_KEY: {"rest": {"max_requests": 0}}})
    assert RATE_LIMIT_CONFIG.rest_max_requests == 0


# ---------------------------------------------------------------------------
# _apply_config — partial updates (missing fields keep current value)
# ---------------------------------------------------------------------------


def test_apply_config_partial_rest_keeps_other_fields():
    original_readings = RATE_LIMIT_CONFIG.rest_max_readings
    _apply_config({CONFIG_KEY: {"rest": {"max_requests": 999}}})
    assert RATE_LIMIT_CONFIG.rest_max_requests == 999
    assert RATE_LIMIT_CONFIG.rest_max_readings == original_readings


def test_apply_config_empty_rest_object_is_no_op():
    snap = _snapshot()
    _apply_config({CONFIG_KEY: {"rest": {}}})
    assert _snapshot() == snap


def test_apply_config_empty_payload_is_no_op():
    snap = _snapshot()
    _apply_config({CONFIG_KEY: {}})
    assert _snapshot() == snap


# ---------------------------------------------------------------------------
# _apply_config — missing key is no-op
# ---------------------------------------------------------------------------


def test_apply_config_missing_rate_limits_key_is_no_op():
    snap = _snapshot()
    _apply_config({"other_key": {"rest": {"max_requests": 1}}})
    assert _snapshot() == snap


def test_apply_config_empty_configs_dict_is_no_op():
    snap = _snapshot()
    _apply_config({})
    assert _snapshot() == snap


# ---------------------------------------------------------------------------
# _apply_config — invalid payload is ignored
# ---------------------------------------------------------------------------


def test_apply_config_invalid_type_string_is_ignored():
    snap = _snapshot()
    _apply_config({CONFIG_KEY: "not a dict"})
    assert _snapshot() == snap


def test_apply_config_invalid_max_requests_type_is_ignored():
    snap = _snapshot()
    # max_requests must be int — float string fails Pydantic coercion
    _apply_config({CONFIG_KEY: {"rest": {"max_requests": "not_a_number"}}})
    assert _snapshot() == snap


def test_apply_config_extra_fields_are_ignored():
    _apply_config({CONFIG_KEY: {
        "rest": {"max_requests": 42, "unknown_field": "ignored"},
        "extra_transport": {"max_requests": 99},
    }})
    assert RATE_LIMIT_CONFIG.rest_max_requests == 42


# ---------------------------------------------------------------------------
# build_config_client
# ---------------------------------------------------------------------------


def test_build_config_client_returns_client_with_subscriber():
    client = build_config_client("http://cfg:8005", poll_interval=1.0)
    assert len(client._subscribers) == 1
    assert client._subscribers[0] is _apply_config


def test_build_config_client_service_name():
    client = build_config_client("http://cfg:8005", poll_interval=1.0)
    assert client.service_name == "telemetry-ingest"


def test_build_config_client_url():
    client = build_config_client("http://cfg:8005", poll_interval=2.0)
    assert "cfg:8005" in client._url
    assert client._poll_interval == 2.0


# ---------------------------------------------------------------------------
# Integration: subscriber fires and mutates config
# ---------------------------------------------------------------------------


def test_subscriber_wired_to_apply_config():
    """Calling subscriber[0] directly with a configs dict applies the config."""
    client = build_config_client("http://cfg:8005", poll_interval=1.0)
    cb = client._subscribers[0]

    cb({CONFIG_KEY: {"rest": {"max_requests": 777}}})
    assert RATE_LIMIT_CONFIG.rest_max_requests == 777
