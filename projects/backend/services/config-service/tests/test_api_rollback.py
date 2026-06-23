"""Integration tests: rollback endpoint."""
from __future__ import annotations

import pytest

from tests.utils import ADMIN_HEADERS


async def _create_ff(client, key="ff_key"):
    resp = await client.post(
        "/api/v1/config",
        json={
            "service_name": "exp-service",
            "key": key,
            "config_type": "feature_flag",
            "value": {"enabled": True},
        },
        headers=ADMIN_HEADERS,
    )
    assert resp.status == 201, await resp.text()
    return await resp.json()


async def _patch(client, config_id, version, value, reason="change"):
    resp = await client.patch(
        f"/api/v1/config/{config_id}",
        json={"version": version, "value": value, "change_reason": reason},
        headers={**ADMIN_HEADERS, "If-Match": f'"{version}"'},
    )
    assert resp.status == 200, await resp.text()
    return await resp.json()


@pytest.mark.asyncio
async def test_rollback_to_v1(service_client):
    data = await _create_ff(service_client)
    config_id = data["id"]

    # v2
    await _patch(service_client, config_id, 1, {"enabled": False})
    # v3
    await _patch(service_client, config_id, 2, {"enabled": True})

    # Rollback v3 → v1
    rb = await service_client.post(
        f"/api/v1/config/{config_id}/rollback",
        json={"version": 3, "target_version": 1, "change_reason": "restore v1"},
        headers={**ADMIN_HEADERS, "If-Match": '"3"'},
    )
    assert rb.status == 200, await rb.text()
    result = await rb.json()
    assert result["version"] == 4
    assert result["value"] == {"enabled": True}


@pytest.mark.asyncio
async def test_rollback_produces_history_entry(service_client):
    data = await _create_ff(service_client, key="rb_hist_key")
    config_id = data["id"]

    await _patch(service_client, config_id, 1, {"enabled": False})

    await service_client.post(
        f"/api/v1/config/{config_id}/rollback",
        json={"version": 2, "target_version": 1, "change_reason": "revert"},
        headers={**ADMIN_HEADERS, "If-Match": '"2"'},
    )

    hist = await service_client.get(
        f"/api/v1/config/{config_id}/history", headers=ADMIN_HEADERS
    )
    items = (await hist.json())["items"]
    assert len(items) == 3
    # History is sorted newest-first (ORDER BY version DESC)
    assert items[0]["change_reason"] == "revert"
    assert items[0]["value"] == {"enabled": True}


@pytest.mark.asyncio
async def test_rollback_to_nonexistent_version(service_client):
    data = await _create_ff(service_client, key="rb_noexist")
    config_id = data["id"]

    rb = await service_client.post(
        f"/api/v1/config/{config_id}/rollback",
        json={"version": 1, "target_version": 99, "change_reason": "bad"},
        headers={**ADMIN_HEADERS, "If-Match": '"1"'},
    )
    assert rb.status == 404


@pytest.mark.asyncio
async def test_rollback_same_version_rejected(service_client):
    """target_version == version is invalid per RollbackRequest validator."""
    data = await _create_ff(service_client, key="rb_same_v")
    config_id = data["id"]

    rb = await service_client.post(
        f"/api/v1/config/{config_id}/rollback",
        json={"version": 1, "target_version": 1, "change_reason": "bad"},
        headers={**ADMIN_HEADERS, "If-Match": '"1"'},
    )
    assert rb.status == 422


@pytest.mark.asyncio
async def test_rollback_chain(service_client):
    """v1 → v2 → rollback→v1 becomes v3, then rollback→v2 becomes v4."""
    data = await _create_ff(service_client, key="rb_chain")
    config_id = data["id"]

    await _patch(service_client, config_id, 1, {"enabled": False}, "v2")

    rb1 = await service_client.post(
        f"/api/v1/config/{config_id}/rollback",
        json={"version": 2, "target_version": 1, "change_reason": "back to v1"},
        headers={**ADMIN_HEADERS, "If-Match": '"2"'},
    )
    assert (await rb1.json())["version"] == 3
    assert (await service_client.get(f"/api/v1/config/{config_id}", headers=ADMIN_HEADERS)).status == 200
