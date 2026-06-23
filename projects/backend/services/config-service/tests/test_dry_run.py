"""Integration tests: dry-run mode."""
from __future__ import annotations

import pytest

from tests.utils import ADMIN_HEADERS

_PAYLOAD = {
    "service_name": "dry-svc",
    "key": "dry_flag",
    "config_type": "feature_flag",
    "value": {"enabled": True},
}


@pytest.mark.asyncio
async def test_dry_run_create_returns_preview(service_client):
    resp = await service_client.post(
        "/api/v1/config?dry_run=true",
        json=_PAYLOAD,
        headers=ADMIN_HEADERS,
    )
    assert resp.status == 200, await resp.text()
    data = await resp.json()
    assert data["dry_run"] is True
    assert "preview" in data
    assert data["preview"]["key"] == "dry_flag"


@pytest.mark.asyncio
async def test_dry_run_create_no_db_write(service_client):
    """After dry-run POST, no config should exist in DB."""
    await service_client.post(
        "/api/v1/config?dry_run=true",
        json=_PAYLOAD,
        headers=ADMIN_HEADERS,
    )

    list_resp = await service_client.get(
        "/api/v1/config?service=dry-svc", headers=ADMIN_HEADERS
    )
    data = await list_resp.json()
    assert len(data["items"]) == 0


@pytest.mark.asyncio
async def test_dry_run_patch_returns_preview(service_client):
    create_resp = await service_client.post(
        "/api/v1/config",
        json=_PAYLOAD,
        headers=ADMIN_HEADERS,
    )
    config_id = (await create_resp.json())["id"]

    resp = await service_client.patch(
        f"/api/v1/config/{config_id}?dry_run=true",
        json={"version": 1, "value": {"enabled": False}, "change_reason": "test"},
        headers={**ADMIN_HEADERS, "If-Match": '"1"'},
    )
    assert resp.status == 200
    data = await resp.json()
    assert data["dry_run"] is True
    assert data["preview"]["value"] == {"enabled": False}


@pytest.mark.asyncio
async def test_dry_run_patch_no_db_write(service_client):
    create_resp = await service_client.post(
        "/api/v1/config",
        json={**_PAYLOAD, "key": "dry_patch_stable"},
        headers=ADMIN_HEADERS,
    )
    config_id = (await create_resp.json())["id"]

    await service_client.patch(
        f"/api/v1/config/{config_id}?dry_run=true",
        json={"version": 1, "value": {"enabled": False}, "change_reason": "test"},
        headers={**ADMIN_HEADERS, "If-Match": '"1"'},
    )

    # Version should still be 1
    get_resp = await service_client.get(
        f"/api/v1/config/{config_id}", headers=ADMIN_HEADERS
    )
    data = await get_resp.json()
    assert data["version"] == 1
    assert data["value"] == {"enabled": True}


@pytest.mark.asyncio
async def test_dry_run_validation_failure(service_client):
    """Dry-run with invalid value returns 422 (no write)."""
    resp = await service_client.post(
        "/api/v1/config?dry_run=true",
        json={**_PAYLOAD, "value": {"enabled": "bad"}},
        headers=ADMIN_HEADERS,
    )
    assert resp.status == 422
