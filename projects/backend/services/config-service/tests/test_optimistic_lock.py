"""Integration tests: optimistic locking."""
from __future__ import annotations

import asyncio

import pytest

from tests.utils import ADMIN_HEADERS, EDITOR_HEADERS


async def _create(client):
    resp = await client.post(
        "/api/v1/config",
        json={
            "service_name": "lock-svc",
            "key": "lock_key",
            "config_type": "feature_flag",
            "value": {"enabled": True},
        },
        headers=ADMIN_HEADERS,
    )
    assert resp.status == 201, await resp.text()
    return await resp.json()


@pytest.mark.asyncio
async def test_missing_if_match_returns_428(service_client):
    data = await _create(service_client)
    config_id = data["id"]

    resp = await service_client.patch(
        f"/api/v1/config/{config_id}",
        json={"version": 1, "value": {"enabled": False}, "change_reason": "test"},
        headers=ADMIN_HEADERS,  # no If-Match
    )
    assert resp.status == 428


@pytest.mark.asyncio
async def test_version_mismatch_between_header_and_body_returns_400(service_client):
    data = await _create(service_client)
    config_id = data["id"]

    resp = await service_client.patch(
        f"/api/v1/config/{config_id}",
        json={"version": 2, "value": {"enabled": False}, "change_reason": "test"},
        headers={**ADMIN_HEADERS, "If-Match": '"1"'},  # header=1, body=2
    )
    assert resp.status == 400


@pytest.mark.asyncio
async def test_stale_version_returns_412(service_client):
    """Patch with already-consumed version → 412 Precondition Failed."""
    data = await _create(service_client)
    config_id = data["id"]

    # First patch succeeds → version becomes 2
    ok = await service_client.patch(
        f"/api/v1/config/{config_id}",
        json={"version": 1, "value": {"enabled": False}, "change_reason": "first"},
        headers={**ADMIN_HEADERS, "If-Match": '"1"'},
    )
    assert ok.status == 200

    # Second patch with old version=1 → 412
    conflict = await service_client.patch(
        f"/api/v1/config/{config_id}",
        json={"version": 1, "value": {"enabled": True}, "change_reason": "stale"},
        headers={**ADMIN_HEADERS, "If-Match": '"1"'},
    )
    assert conflict.status == 412


@pytest.mark.asyncio
async def test_concurrent_patches_one_wins(service_client):
    """Two concurrent PATCHes with version=1; exactly one should succeed."""
    data = await _create(service_client)
    config_id = data["id"]

    async def do_patch():
        return await service_client.patch(
            f"/api/v1/config/{config_id}",
            json={"version": 1, "value": {"enabled": False}, "change_reason": "race"},
            headers={**ADMIN_HEADERS, "If-Match": '"1"'},
        )

    results = await asyncio.gather(do_patch(), do_patch(), return_exceptions=True)
    statuses = [r.status for r in results if hasattr(r, "status")]
    assert 200 in statuses
    assert 412 in statuses


@pytest.mark.asyncio
async def test_delete_stale_version_returns_412(service_client):
    data = await _create(service_client)
    config_id = data["id"]

    # Move version to 2
    await service_client.patch(
        f"/api/v1/config/{config_id}",
        json={"version": 1, "value": {"enabled": False}, "change_reason": "bump"},
        headers={**ADMIN_HEADERS, "If-Match": '"1"'},
    )

    # Try delete with old version
    resp = await service_client.delete(
        f"/api/v1/config/{config_id}?version=1&change_reason=cleanup",
        headers={**ADMIN_HEADERS, "If-Match": '"1"'},
    )
    assert resp.status == 412
