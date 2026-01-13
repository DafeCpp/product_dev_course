from __future__ import annotations

import unittest
from datetime import datetime, timezone
from typing import Any
from unittest.mock import patch
from uuid import UUID

from telemetry_cli.models import TelemetryReading
from telemetry_cli.sink_experiment_service import ExperimentServiceClient


class _FakeResponse:
    def __init__(self, payload: dict[str, Any]):
        self._payload = payload

    def raise_for_status(self) -> None:
        return None

    def json(self) -> dict[str, Any]:
        return self._payload


class _FakeAsyncClient:
    last_post: dict[str, Any] | None = None
    post_calls: int = 0

    def __init__(self, **_kwargs: Any):
        return None

    async def post(self, url: str, *, json: dict[str, Any], headers: dict[str, str]) -> _FakeResponse:  # noqa: A002
        type(self).post_calls += 1
        type(self).last_post = {"url": url, "json": json, "headers": headers}
        return _FakeResponse({"status": "ok"})

    async def aclose(self) -> None:
        return None


class TestExperimentServiceClient(unittest.IsolatedAsyncioTestCase):
    async def test_ingest_skips_empty_batch_without_http_call(self) -> None:
        _FakeAsyncClient.last_post = None
        _FakeAsyncClient.post_calls = 0

        with patch("telemetry_cli.sink_experiment_service.httpx.AsyncClient", _FakeAsyncClient):
            async with ExperimentServiceClient(base_url="http://localhost:8003", sensor_token="t") as client:
                res = await client.ingest(
                    sensor_id=UUID(int=1),
                    run_id=None,
                    capture_session_id=None,
                    meta={},
                    readings=[],
                )

        self.assertEqual(res, {"status": "skipped", "accepted": 0})
        self.assertEqual(_FakeAsyncClient.post_calls, 0)

    async def test_ingest_posts_expected_payload_and_headers(self) -> None:
        _FakeAsyncClient.last_post = None
        _FakeAsyncClient.post_calls = 0

        reading = TelemetryReading(
            timestamp=datetime(2026, 1, 1, 0, 0, 0, tzinfo=timezone.utc),
            raw_value=1.23,
            signal="imu.ax",
            meta={"seq": 7},
        )

        with patch("telemetry_cli.sink_experiment_service.httpx.AsyncClient", _FakeAsyncClient):
            async with ExperimentServiceClient(base_url="http://localhost:8003", sensor_token="secret-token") as client:
                res = await client.ingest(
                    sensor_id=UUID(int=1),
                    run_id=None,
                    capture_session_id=None,
                    meta={"vehicle_type": "rc_car"},
                    readings=[reading],
                )

        self.assertEqual(res["status"], "ok")
        self.assertEqual(_FakeAsyncClient.post_calls, 1)
        assert _FakeAsyncClient.last_post is not None

        self.assertEqual(_FakeAsyncClient.last_post["url"], "http://localhost:8003/api/v1/telemetry")
        self.assertEqual(_FakeAsyncClient.last_post["headers"]["Authorization"], "Bearer secret-token")

        payload = _FakeAsyncClient.last_post["json"]
        self.assertEqual(payload["sensor_id"], str(UUID(int=1)))
        self.assertEqual(payload["meta"]["vehicle_type"], "rc_car")
        self.assertEqual(len(payload["readings"]), 1)
        self.assertTrue(payload["readings"][0]["timestamp"].endswith("Z"))
        self.assertEqual(payload["readings"][0]["raw_value"], 1.23)
        self.assertEqual(payload["readings"][0]["meta"]["signal"], "imu.ax")
        self.assertEqual(payload["readings"][0]["meta"]["seq"], 7)

