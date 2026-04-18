from __future__ import annotations

import pytest


async def test_health_ok(service_client):
    resp = await service_client.get("/health")
    assert resp.status == 200
    payload = await resp.json()
    assert payload["status"] == "ok"


async def test_health_includes_db_status(service_client):
    resp = await service_client.get("/health")
    assert resp.status == 200
    payload = await resp.json()
    assert payload["db"]["status"] == "ok"


async def test_health_includes_spool_info(service_client):
    resp = await service_client.get("/health")
    assert resp.status == 200
    payload = await resp.json()
    assert "spool" in payload
    assert "depth" in payload["spool"]
    assert isinstance(payload["spool"]["depth"], int)
    assert "enabled" in payload["spool"]


async def test_health_includes_uptime(service_client):
    resp = await service_client.get("/health")
    assert resp.status == 200
    payload = await resp.json()
    assert "uptime_seconds" in payload
    assert payload["uptime_seconds"] >= 0


async def test_health_returns_503_when_db_unavailable(service_client, monkeypatch):
    """When the DB pool raises, /health returns 503 with status=degraded."""
    import telemetry_ingest_service.api.routes.health as health_module

    async def _bad_pool():
        raise RuntimeError("simulated DB failure")

    monkeypatch.setattr(health_module, "get_pool", _bad_pool)

    resp = await service_client.get("/health")
    assert resp.status == 503
    payload = await resp.json()
    assert payload["status"] == "degraded"
    assert payload["db"]["status"] == "error"
    assert "error" in payload["db"]
