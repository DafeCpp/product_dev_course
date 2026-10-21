"""experiment_qos settings from config-service applied to request handling.

- ``rate_limit_max_requests`` caps GET /experiments and POST /runs per user,
  read on every request (a QoS change applies without a restart).
- ``downstream_timeout_seconds`` is the timeout of each audit write.
"""
from __future__ import annotations

import uuid
from contextlib import asynccontextmanager
from unittest.mock import AsyncMock, MagicMock

import pytest
from aiohttp import ClientSession, web

from experiment_service.middleware import api_rate_limit
from experiment_service.middleware.export_rate_limit import ExportRateLimiter
from experiment_service.middleware.qos_config import QOS_CONFIG
from experiment_service.services.audit_client import AuditClient
from tests.utils import make_headers


@pytest.fixture
def fresh_api_limiter(monkeypatch):
    """A clean QoS-driven limiter so earlier requests in the session don't count."""
    limiter = ExportRateLimiter(
        max_requests=lambda: QOS_CONFIG.rate_limit_max_requests,
        window_seconds=api_rate_limit.API_RATE_LIMIT_WINDOW_SECONDS,
    )
    monkeypatch.setattr(api_rate_limit, "api_rate_limiter", limiter)
    return limiter


def test_limiter_reads_callable_limit_on_every_check():
    limit = {"value": 1}
    limiter = ExportRateLimiter(max_requests=lambda: limit["value"], window_seconds=60.0)
    uid = uuid.uuid4()

    limiter.check(uid)
    with pytest.raises(web.HTTPTooManyRequests):
        limiter.check(uid)

    limit["value"] = 5
    limiter.check(uid)  # raised limit applies immediately


def test_api_limiter_follows_qos_config(monkeypatch, fresh_api_limiter):
    monkeypatch.setattr(QOS_CONFIG, "rate_limit_max_requests", 1)
    uid = uuid.uuid4()

    api_rate_limit.api_rate_limiter.check(uid)
    with pytest.raises(web.HTTPTooManyRequests) as exc:
        api_rate_limit.api_rate_limiter.check(uid)
    assert exc.value.headers["X-RateLimit-Limit"] == "1"


@pytest.mark.asyncio
async def test_list_experiments_returns_429_over_qos_limit(
    service_client, monkeypatch, fresh_api_limiter
):
    monkeypatch.setattr(QOS_CONFIG, "rate_limit_max_requests", 2)
    headers = make_headers(uuid.uuid4())

    for _ in range(2):
        resp = await service_client.get("/api/v1/experiments", headers=headers)
        assert resp.status == 200, await resp.text()

    resp = await service_client.get("/api/v1/experiments", headers=headers)
    assert resp.status == 429
    assert resp.headers["X-RateLimit-Limit"] == "2"
    assert "Retry-After" in resp.headers

    # Raising the limit in config-service lifts the block without a restart.
    monkeypatch.setattr(QOS_CONFIG, "rate_limit_max_requests", 10)
    resp = await service_client.get("/api/v1/experiments", headers=headers)
    assert resp.status == 200


@pytest.mark.asyncio
async def test_create_run_shares_the_qos_budget(service_client, monkeypatch, fresh_api_limiter):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "QoS"},
        headers=headers,
    )
    assert resp.status == 201
    experiment_id = (await resp.json())["id"]

    monkeypatch.setattr(QOS_CONFIG, "rate_limit_max_requests", 1)
    resp = await service_client.get("/api/v1/experiments", headers=headers)
    assert resp.status == 200

    resp = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/runs",
        json={"name": "run"},
        headers=headers,
    )
    assert resp.status == 429


@pytest.mark.asyncio
async def test_audit_write_uses_qos_downstream_timeout(monkeypatch):
    monkeypatch.setattr(QOS_CONFIG, "downstream_timeout_seconds", 1.5)
    response = MagicMock(status=201)
    response.text = AsyncMock(return_value="")
    calls: list[dict] = []

    @asynccontextmanager
    async def fake_post(url, **kwargs):
        calls.append(kwargs)
        yield response

    session = MagicMock(spec=ClientSession)
    session.post = fake_post
    client = AuditClient("http://auth-service:8001", session)

    await client._send({"action": "experiment.create"})

    assert calls[0]["timeout"].total == 1.5
