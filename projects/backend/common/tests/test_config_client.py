"""Tests for backend_common.config_client.ConfigClient."""
from __future__ import annotations

import asyncio
import json
import os
import time
from pathlib import Path
from typing import Any
from unittest.mock import AsyncMock, MagicMock, patch

import pytest

from backend_common.config_client import ConfigClient


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_response(
    status: int = 200,
    configs: dict[str, Any] | None = None,
    etag: str | None = '"v1"',
    last_modified: str | None = None,
) -> MagicMock:
    """Build a mock aiohttp response context manager."""
    resp = MagicMock()
    resp.status = status
    resp.headers = {}
    if etag:
        resp.headers["ETag"] = etag
    if last_modified:
        resp.headers["Last-Modified"] = last_modified
    resp.json = AsyncMock(return_value={"configs": configs or {}})
    resp.__aenter__ = AsyncMock(return_value=resp)
    resp.__aexit__ = AsyncMock(return_value=False)
    return resp


def _make_session(response: MagicMock) -> MagicMock:
    session = MagicMock()
    session.get = MagicMock(return_value=response)
    session.close = AsyncMock()
    return session


# ---------------------------------------------------------------------------
# get() — priority chain
# ---------------------------------------------------------------------------


def test_get_returns_default_when_cache_empty():
    client = ConfigClient("svc", "http://cfg:8005")
    assert client.get("missing", default=42) == 42


def test_get_returns_none_when_no_default():
    client = ConfigClient("svc", "http://cfg:8005")
    assert client.get("missing") is None


def test_get_returns_cache_value():
    client = ConfigClient("svc", "http://cfg:8005")
    client._cache = {"rate_limit": 600}
    assert client.get("rate_limit", default=60) == 600


def test_get_env_override_takes_priority(monkeypatch: pytest.MonkeyPatch):
    client = ConfigClient("svc", "http://cfg:8005")
    client._cache = {"rate_limit": 600}
    monkeypatch.setenv("CONFIG_OVERRIDE__SVC__RATE_LIMIT", "999")
    assert client.get("rate_limit") == 999


def test_get_env_override_returns_string_when_not_json(monkeypatch: pytest.MonkeyPatch):
    client = ConfigClient("svc", "http://cfg:8005")
    monkeypatch.setenv("CONFIG_OVERRIDE__SVC__MODE", "debug")
    assert client.get("mode") == "debug"


def test_get_env_override_parses_bool(monkeypatch: pytest.MonkeyPatch):
    client = ConfigClient("svc", "http://cfg:8005")
    monkeypatch.setenv("CONFIG_OVERRIDE__SVC__ENABLED", "true")
    assert client.get("enabled") is True


def test_get_env_override_service_name_normalised(monkeypatch: pytest.MonkeyPatch):
    client = ConfigClient("telemetry-ingest", "http://cfg:8005")
    monkeypatch.setenv("CONFIG_OVERRIDE__TELEMETRY_INGEST__REST_RATE_LIMIT", "1")
    assert client.get("rest_rate_limit") == 1


# ---------------------------------------------------------------------------
# _poll_once — 200 response
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_poll_once_200_updates_cache():
    client = ConfigClient("svc", "http://cfg:8005")
    resp = _make_response(200, configs={"key": "val"}, etag='"etag1"')
    client._session = _make_session(resp)

    await client._poll_once()

    assert client._cache == {"key": "val"}
    assert client._etag == '"etag1"'


@pytest.mark.asyncio
async def test_poll_once_200_sends_if_none_match_on_second_call():
    client = ConfigClient("svc", "http://cfg:8005")
    client._etag = '"prev"'
    resp = _make_response(200, configs={})
    session = _make_session(resp)
    client._session = session

    await client._poll_once()

    call_kwargs = session.get.call_args[1]
    assert call_kwargs["headers"]["If-None-Match"] == '"prev"'


@pytest.mark.asyncio
async def test_poll_once_304_no_change():
    client = ConfigClient("svc", "http://cfg:8005")
    client._cache = {"old": "data"}
    client._etag = '"v1"'
    resp = _make_response(304)
    client._session = _make_session(resp)

    await client._poll_once()

    assert client._cache == {"old": "data"}
    assert client._etag == '"v1"'


@pytest.mark.asyncio
async def test_poll_once_non_200_404_does_not_update():
    client = ConfigClient("svc", "http://cfg:8005")
    client._cache = {"old": "data"}
    resp = _make_response(404)
    client._session = _make_session(resp)

    await client._poll_once()

    assert client._cache == {"old": "data"}


# ---------------------------------------------------------------------------
# Subscribers
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_poll_once_200_notifies_subscribers():
    received: list[dict] = []

    def callback(configs: dict) -> None:
        received.append(configs)

    client = ConfigClient("svc", "http://cfg:8005")
    client.subscribe(callback)
    resp = _make_response(200, configs={"x": 1})
    client._session = _make_session(resp)

    await client._poll_once()

    assert received == [{"x": 1}]


@pytest.mark.asyncio
async def test_poll_once_304_does_not_notify_subscribers():
    called = [False]

    def callback(configs: dict) -> None:
        called[0] = True

    client = ConfigClient("svc", "http://cfg:8005")
    client.subscribe(callback)
    resp = _make_response(304)
    client._session = _make_session(resp)

    await client._poll_once()

    assert not called[0]


@pytest.mark.asyncio
async def test_async_subscriber_is_awaited():
    received: list[dict] = []

    async def async_cb(configs: dict) -> None:
        received.append(configs)

    client = ConfigClient("svc", "http://cfg:8005")
    client.subscribe(async_cb)
    resp = _make_response(200, configs={"y": 2})
    client._session = _make_session(resp)

    await client._poll_once()

    assert received == [{"y": 2}]


@pytest.mark.asyncio
async def test_failing_subscriber_does_not_block_others():
    results: list[str] = []

    def bad_cb(configs: dict) -> None:
        raise RuntimeError("boom")

    def good_cb(configs: dict) -> None:
        results.append("ok")

    client = ConfigClient("svc", "http://cfg:8005")
    client.subscribe(bad_cb)
    client.subscribe(good_cb)
    resp = _make_response(200, configs={})
    client._session = _make_session(resp)

    await client._poll_once()

    assert results == ["ok"]


# ---------------------------------------------------------------------------
# Fallback file
# ---------------------------------------------------------------------------


def test_cold_start_loads_fallback_file(tmp_path: Path):
    fallback = tmp_path / "svc.json"
    fallback.write_text(json.dumps({"cold_key": "cold_val"}))

    client = ConfigClient("svc", "http://cfg:8005", fallback_dir=tmp_path)

    assert client._cache == {"cold_key": "cold_val"}
    assert client.get("cold_key") == "cold_val"


def test_missing_fallback_file_is_ignored(tmp_path: Path):
    client = ConfigClient("svc", "http://cfg:8005", fallback_dir=tmp_path)
    assert client._cache == {}


def test_corrupt_fallback_file_is_ignored(tmp_path: Path):
    fallback = tmp_path / "svc.json"
    fallback.write_text("not json {{{")

    client = ConfigClient("svc", "http://cfg:8005", fallback_dir=tmp_path)
    assert client._cache == {}


@pytest.mark.asyncio
async def test_200_poll_saves_fallback_atomically(tmp_path: Path):
    client = ConfigClient("svc", "http://cfg:8005", fallback_dir=tmp_path)
    resp = _make_response(200, configs={"saved": True})
    client._session = _make_session(resp)

    await client._poll_once()

    fallback = tmp_path / "svc.json"
    assert fallback.exists()
    assert json.loads(fallback.read_text()) == {"saved": True}


@pytest.mark.asyncio
async def test_fallback_not_saved_on_304(tmp_path: Path):
    client = ConfigClient("svc", "http://cfg:8005", fallback_dir=tmp_path)
    resp = _make_response(304)
    client._session = _make_session(resp)

    await client._poll_once()

    assert not (tmp_path / "svc.json").exists()


# ---------------------------------------------------------------------------
# Propagation lag metric
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_propagation_lag_recorded_on_200(tmp_path: Path):
    import time
    from email.utils import formatdate

    last_modified = formatdate(time.time() - 5.0, usegmt=True)
    client = ConfigClient("svc-lag", "http://cfg:8005", fallback_dir=tmp_path)
    resp = _make_response(200, configs={}, last_modified=last_modified)
    client._session = _make_session(resp)

    with patch("backend_common.config_client.client._PROPAGATION_LAG") as mock_hist:
        mock_hist.labels = MagicMock(return_value=MagicMock(observe=MagicMock()))
        await client._poll_once()
        mock_hist.labels.assert_called_once_with(service="svc-lag")
        obs = mock_hist.labels.return_value.observe
        obs.assert_called_once()
        observed_lag = obs.call_args[0][0]
        assert observed_lag >= 0.0


# ---------------------------------------------------------------------------
# Lifecycle: start / stop
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_start_creates_poll_task():
    client = ConfigClient("svc", "http://cfg:8005")
    mock_session = MagicMock()
    mock_session.close = AsyncMock()

    with patch("aiohttp.ClientSession", return_value=mock_session):
        await client.start()
        assert client._task is not None
        assert not client._task.done()
        await client.stop()


@pytest.mark.asyncio
async def test_stop_cancels_task():
    client = ConfigClient("svc", "http://cfg:8005")
    mock_session = MagicMock()
    mock_session.close = AsyncMock()

    with patch("aiohttp.ClientSession", return_value=mock_session):
        await client.start()
        task = client._task
        await client.stop()
        assert task is not None
        assert task.done()


@pytest.mark.asyncio
async def test_start_is_idempotent():
    client = ConfigClient("svc", "http://cfg:8005")
    mock_session = MagicMock()
    mock_session.close = AsyncMock()

    with patch("aiohttp.ClientSession", return_value=mock_session):
        await client.start()
        task1 = client._task
        await client.start()
        task2 = client._task
        assert task1 is task2
        await client.stop()


@pytest.mark.asyncio
async def test_stop_without_start_is_safe():
    client = ConfigClient("svc", "http://cfg:8005")
    await client.stop()  # must not raise


@pytest.mark.asyncio
async def test_external_session_not_closed_on_stop():
    ext_session = MagicMock()
    ext_session.close = AsyncMock()

    client = ConfigClient("svc", "http://cfg:8005", session=ext_session)
    await client.start()
    await client.stop()

    ext_session.close.assert_not_called()


# ---------------------------------------------------------------------------
# Polling loop: fail-open / backoff
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_poll_loop_continues_after_network_error(tmp_path: Path):
    """Cache survives a network error; last-known-good is still returned."""
    client = ConfigClient("svc", "http://cfg:8005", poll_interval=0.05, fallback_dir=tmp_path)
    client._cache = {"last_known": "good"}

    call_count = [0]
    original_poll = client._poll_once

    async def failing_then_ok() -> None:
        call_count[0] += 1
        if call_count[0] == 1:
            raise aiohttp.ClientConnectionError("refused")
        await original_poll()

    mock_session = MagicMock()
    mock_session.close = AsyncMock()

    with patch("aiohttp.ClientSession", return_value=mock_session), \
         patch.object(client, "_poll_once", side_effect=failing_then_ok):
        await client.start()
        await asyncio.sleep(0.3)
        await client.stop()

    # After error the old cache is untouched.
    assert client.get("last_known") == "good"
