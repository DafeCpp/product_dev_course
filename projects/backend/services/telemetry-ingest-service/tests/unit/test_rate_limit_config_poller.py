"""Unit tests for workers.config_poller (_apply_config + build_config_client)."""
from __future__ import annotations

import pytest

from telemetry_ingest_service.middleware.rate_limit_config import RATE_LIMIT_CONFIG
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
        "spool_flush_timeout_seconds": RATE_LIMIT_CONFIG.spool_flush_timeout_seconds,
        "ws_max_message_bytes": RATE_LIMIT_CONFIG.ws_max_message_bytes,
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
# _apply_config — happy path (existing REST/WS limits)
# ---------------------------------------------------------------------------


def test_apply_config_updates_rest_max_requests():
    _apply_config({CONFIG_KEY: {"rest": {"max_requests": 500}}})
    assert RATE_LIMIT_CONFIG.rest_max_requests == 500


def test_apply_config_updates_ws_max_messages():
    _apply_config({CONFIG_KEY: {"ws": {"max_messages": 300}}})
    assert RATE_LIMIT_CONFIG.ws_max_messages == 300


# ---------------------------------------------------------------------------
# _apply_config — happy path (new spool/message-size parameters)
# ---------------------------------------------------------------------------


def test_apply_config_updates_spool_flush_timeout():
    _apply_config({CONFIG_KEY: {"spool_flush_timeout_seconds": 10.0}})
    assert RATE_LIMIT_CONFIG.spool_flush_timeout_seconds == 10.0


def test_apply_config_updates_ws_max_message_bytes():
    _apply_config({CONFIG_KEY: {"ws_max_message_bytes": 2097152}})
    assert RATE_LIMIT_CONFIG.ws_max_message_bytes == 2097152


def test_apply_config_updates_all_fields():
    _apply_config({CONFIG_KEY: {
        "rest": {"max_requests": 1000, "max_readings": 100000, "window_seconds": 120.0},
        "ws": {"max_messages": 500, "max_readings": 50000, "window_seconds": 2.0},
        "spool_flush_timeout_seconds": 7.5,
        "ws_max_message_bytes": 1048576,
    }})
    assert RATE_LIMIT_CONFIG.rest_max_requests == 1000
    assert RATE_LIMIT_CONFIG.rest_max_readings == 100000
    assert RATE_LIMIT_CONFIG.rest_window_seconds == 120.0
    assert RATE_LIMIT_CONFIG.ws_max_messages == 500
    assert RATE_LIMIT_CONFIG.ws_max_readings == 50000
    assert RATE_LIMIT_CONFIG.ws_window_seconds == 2.0
    assert RATE_LIMIT_CONFIG.spool_flush_timeout_seconds == 7.5
    assert RATE_LIMIT_CONFIG.ws_max_message_bytes == 1048576


# ---------------------------------------------------------------------------
# _apply_config — partial updates (missing fields keep current value)
# ---------------------------------------------------------------------------


def test_apply_config_partial_update_spool_keeps_other_fields():
    original_rest = RATE_LIMIT_CONFIG.rest_max_requests
    original_ws_bytes = RATE_LIMIT_CONFIG.ws_max_message_bytes
    _apply_config({CONFIG_KEY: {"spool_flush_timeout_seconds": 12.0}})
    assert RATE_LIMIT_CONFIG.spool_flush_timeout_seconds == 12.0
    assert RATE_LIMIT_CONFIG.rest_max_requests == original_rest
    assert RATE_LIMIT_CONFIG.ws_max_message_bytes == original_ws_bytes


def test_apply_config_partial_update_message_bytes_keeps_other_fields():
    original_spool = RATE_LIMIT_CONFIG.spool_flush_timeout_seconds
    _apply_config({CONFIG_KEY: {"ws_max_message_bytes": 524288}})
    assert RATE_LIMIT_CONFIG.ws_max_message_bytes == 524288
    assert RATE_LIMIT_CONFIG.spool_flush_timeout_seconds == original_spool


def test_apply_config_empty_payload_is_no_op():
    snap = _snapshot()
    _apply_config({CONFIG_KEY: {}})
    assert _snapshot() == snap


# ---------------------------------------------------------------------------
# _apply_config — missing key is no-op
# ---------------------------------------------------------------------------


def test_apply_config_missing_rate_limits_key_is_no_op():
    snap = _snapshot()
    _apply_config({"other_key": {"spool_flush_timeout_seconds": 999}})
    assert _snapshot() == snap


def test_apply_config_empty_configs_dict_is_no_op():
    snap = _snapshot()
    _apply_config({})
    assert _snapshot() == snap


# ---------------------------------------------------------------------------
# _apply_config — invalid payload is ignored
# ---------------------------------------------------------------------------


def test_apply_config_invalid_spool_type_is_ignored():
    snap = _snapshot()
    # spool_flush_timeout_seconds must be float — string fails Pydantic coercion
    _apply_config({CONFIG_KEY: {"spool_flush_timeout_seconds": "not_a_number"}})
    assert _snapshot() == snap


def test_apply_config_invalid_message_bytes_type_is_ignored():
    snap = _snapshot()
    # ws_max_message_bytes must be int — string fails Pydantic coercion
    _apply_config({CONFIG_KEY: {"ws_max_message_bytes": "not_an_int"}})
    assert _snapshot() == snap


def test_apply_config_extra_fields_are_ignored():
    _apply_config({CONFIG_KEY: {
        "spool_flush_timeout_seconds": 8.0,
        "unknown_field": "ignored",
    }})
    assert RATE_LIMIT_CONFIG.spool_flush_timeout_seconds == 8.0


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
