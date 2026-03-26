"""Unit tests for the Prometheus /metrics endpoint."""
from __future__ import annotations

import pytest
from aiohttp import web

from backend_common.metrics import metrics_handler, metrics_middleware


@pytest.fixture
def metrics_app() -> web.Application:
    """Minimal aiohttp app with only the metrics route wired up."""
    app = web.Application()
    app.middlewares.append(metrics_middleware("auth-service"))
    app.router.add_get("/metrics", metrics_handler)
    return app


@pytest.mark.asyncio
async def test_metrics_endpoint_returns_200(aiohttp_client, metrics_app: web.Application) -> None:
    client = await aiohttp_client(metrics_app)
    response = await client.get("/metrics")
    assert response.status == 200


@pytest.mark.asyncio
async def test_metrics_endpoint_content_type(aiohttp_client, metrics_app: web.Application) -> None:
    client = await aiohttp_client(metrics_app)
    response = await client.get("/metrics")
    assert "text/plain" in response.headers["Content-Type"]


@pytest.mark.asyncio
async def test_metrics_endpoint_contains_process_metrics(
    aiohttp_client, metrics_app: web.Application
) -> None:
    client = await aiohttp_client(metrics_app)
    response = await client.get("/metrics")
    text = await response.text()
    # prometheus_client always exposes process_ and python_ metrics by default
    assert "process_" in text or "python_" in text


@pytest.mark.asyncio
async def test_metrics_middleware_records_request_count(
    aiohttp_client, metrics_app: web.Application
) -> None:
    """Hitting /metrics itself is excluded; add a dummy endpoint to verify counter."""

    async def dummy(_request: web.Request) -> web.Response:
        return web.Response(text="ok")

    metrics_app.router.add_get("/ping", dummy)
    client = await aiohttp_client(metrics_app)

    await client.get("/ping")

    response = await client.get("/metrics")
    assert response.status == 200
    text = await response.text()
    assert "http_requests_total" in text
    assert 'service="auth-service"' in text
