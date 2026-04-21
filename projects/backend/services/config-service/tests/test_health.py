"""Integration tests: health endpoint."""
from __future__ import annotations

import pytest


@pytest.mark.asyncio
async def test_health_ok(service_client):
    resp = await service_client.get("/health")
    assert resp.status == 200
    data = await resp.json()
    assert data["status"] == "ok"


@pytest.mark.asyncio
async def test_health_no_auth_required(service_client):
    """Health endpoint must not require authentication."""
    resp = await service_client.get("/health")
    assert resp.status == 200
