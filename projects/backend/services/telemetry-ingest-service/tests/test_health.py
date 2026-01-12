from __future__ import annotations


async def test_health_ok(service_client):
    resp = await service_client.get("/health")
    assert resp.status == 200
    payload = await resp.json()
    assert payload["status"] == "ok"

