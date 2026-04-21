"""Integration tests: bulk endpoint."""
from __future__ import annotations

import pytest

from tests.utils import ADMIN_HEADERS

_SERVICE = "bulk-svc"


async def _create_ff(client, key, enabled=True):
    resp = await client.post(
        "/api/v1/config",
        json={
            "service_name": _SERVICE,
            "key": key,
            "config_type": "feature_flag",
            "value": {"enabled": enabled},
        },
        headers=ADMIN_HEADERS,
    )
    assert resp.status == 201, await resp.text()
    return await resp.json()


@pytest.mark.asyncio
async def test_bulk_returns_active_configs(service_client):
    await _create_ff(service_client, "flag_a")
    await _create_ff(service_client, "flag_b", enabled=False)

    resp = await service_client.get(
        f"/api/v1/configs/bulk?service={_SERVICE}"
    )
    assert resp.status == 200, await resp.text()
    data = await resp.json()
    assert "configs" in data
    assert "flag_a" in data["configs"]
    assert "ETag" in resp.headers


@pytest.mark.asyncio
async def test_bulk_requires_service_param(service_client):
    resp = await service_client.get("/api/v1/configs/bulk")
    assert resp.status == 400


@pytest.mark.asyncio
async def test_bulk_304_on_same_etag(service_client):
    await _create_ff(service_client, "flag_etag")

    resp1 = await service_client.get(f"/api/v1/configs/bulk?service={_SERVICE}")
    etag = resp1.headers["ETag"]

    resp2 = await service_client.get(
        f"/api/v1/configs/bulk?service={_SERVICE}",
        headers={"If-None-Match": etag},
    )
    assert resp2.status == 304
    assert resp2.headers.get("ETag") == etag


@pytest.mark.asyncio
async def test_bulk_200_after_update(service_client):
    data = await _create_ff(service_client, "flag_change")
    config_id = data["id"]

    resp1 = await service_client.get(f"/api/v1/configs/bulk?service={_SERVICE}")
    etag1 = resp1.headers["ETag"]

    # Patch the config
    await service_client.patch(
        f"/api/v1/config/{config_id}",
        json={"version": 1, "value": {"enabled": False}, "change_reason": "test"},
        headers={**ADMIN_HEADERS, "If-Match": '"1"'},
    )

    resp2 = await service_client.get(
        f"/api/v1/configs/bulk?service={_SERVICE}",
        headers={"If-None-Match": etag1},
    )
    assert resp2.status == 200
    etag2 = resp2.headers["ETag"]
    assert etag2 != etag1


@pytest.mark.asyncio
async def test_bulk_empty_service(service_client):
    resp = await service_client.get("/api/v1/configs/bulk?service=nonexistent-svc-xyz")
    assert resp.status == 200
    data = await resp.json()
    assert data["configs"] == {}
