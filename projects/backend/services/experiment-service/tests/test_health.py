from __future__ import annotations

import pytest


@pytest.mark.asyncio
async def test_healthcheck(service_client):
    response = await service_client.get("/health")
    assert response.status == 200
    payload = await response.json()
    assert payload["status"] == "ok"


@pytest.mark.asyncio
async def test_health_includes_db_status(service_client):
    response = await service_client.get("/health")
    assert response.status == 200
    payload = await response.json()
    assert payload["db"]["status"] == "ok"


@pytest.mark.asyncio
async def test_health_includes_uptime(service_client):
    response = await service_client.get("/health")
    assert response.status == 200
    payload = await response.json()
    assert "uptime_seconds" in payload
    assert payload["uptime_seconds"] >= 0


@pytest.mark.asyncio
async def test_health_returns_503_when_db_unavailable(service_client, monkeypatch):
    from backend_common.db import pool as pool_module

    async def _bad_pool():
        raise RuntimeError("simulated DB failure")

    monkeypatch.setattr(pool_module, "get_pool_service", _bad_pool)
    import experiment_service.api.routes.health as health_module
    monkeypatch.setattr(health_module, "get_pool", _bad_pool)

    response = await service_client.get("/health")
    assert response.status == 503
    payload = await response.json()
    assert payload["status"] == "degraded"
    assert payload["db"]["status"] == "error"
    assert "error" in payload["db"]
