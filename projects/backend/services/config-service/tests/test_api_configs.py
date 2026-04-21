"""Integration tests: config CRUD lifecycle."""
from __future__ import annotations

import pytest

from tests.utils import ADMIN_HEADERS, EDITOR_HEADERS, VIEWER_HEADERS, make_headers

_BASE_PAYLOAD = {
    "service_name": "auth-service",
    "key": "write_path_enabled",
    "config_type": "feature_flag",
    "value": {"enabled": True},
}


async def _create(client, payload=None, headers=None, idempotency_key=None):
    payload = payload or _BASE_PAYLOAD
    hdrs = {**(headers or ADMIN_HEADERS)}
    if idempotency_key:
        hdrs["Idempotency-Key"] = idempotency_key
    resp = await client.post("/api/v1/config", json=payload, headers=hdrs)
    return resp


@pytest.mark.asyncio
async def test_create_and_get(service_client):
    resp = await _create(service_client)
    assert resp.status == 201, await resp.text()
    data = await resp.json()
    assert data["key"] == "write_path_enabled"
    assert data["version"] == 1
    assert resp.headers.get("ETag") == '"1"'

    config_id = data["id"]
    get_resp = await service_client.get(
        f"/api/v1/config/{config_id}", headers=ADMIN_HEADERS
    )
    assert get_resp.status == 200
    got = await get_resp.json()
    assert got["id"] == config_id
    assert got["value"] == {"enabled": True}


@pytest.mark.asyncio
async def test_list_configs(service_client):
    await _create(service_client)
    resp = await service_client.get(
        "/api/v1/config?service=auth-service", headers=ADMIN_HEADERS
    )
    assert resp.status == 200
    data = await resp.json()
    assert len(data["items"]) >= 1


@pytest.mark.asyncio
async def test_patch_config(service_client):
    resp = await _create(service_client)
    data = await resp.json()
    config_id = data["id"]

    patch_resp = await service_client.patch(
        f"/api/v1/config/{config_id}",
        json={"version": 1, "value": {"enabled": False}, "change_reason": "INC-001"},
        headers={**EDITOR_HEADERS, "If-Match": '"1"'},
    )
    assert patch_resp.status == 200, await patch_resp.text()
    patched = await patch_resp.json()
    assert patched["version"] == 2
    assert patched["value"] == {"enabled": False}
    assert patch_resp.headers.get("ETag") == '"2"'


@pytest.mark.asyncio
async def test_patch_increments_version(service_client):
    resp = await _create(service_client)
    config_id = (await resp.json())["id"]

    for i in range(1, 4):
        patch_resp = await service_client.patch(
            f"/api/v1/config/{config_id}",
            json={
                "version": i,
                "value": {"enabled": i % 2 == 0},
                "change_reason": f"step-{i}",
            },
            headers={**ADMIN_HEADERS, "If-Match": f'"{i}"'},
        )
        assert patch_resp.status == 200
        assert (await patch_resp.json())["version"] == i + 1


@pytest.mark.asyncio
async def test_soft_delete(service_client):
    resp = await _create(service_client)
    config_id = (await resp.json())["id"]

    del_resp = await service_client.delete(
        f"/api/v1/config/{config_id}?version=1&change_reason=cleanup",
        headers={**ADMIN_HEADERS, "If-Match": '"1"'},
    )
    assert del_resp.status == 204

    get_resp = await service_client.get(
        f"/api/v1/config/{config_id}", headers=ADMIN_HEADERS
    )
    assert get_resp.status == 404


@pytest.mark.asyncio
async def test_recreate_after_soft_delete(service_client):
    """Same (service, key) can be created after soft-delete."""
    resp = await _create(service_client)
    config_id = (await resp.json())["id"]

    await service_client.delete(
        f"/api/v1/config/{config_id}?version=1&change_reason=cleanup",
        headers={**ADMIN_HEADERS, "If-Match": '"1"'},
    )

    resp2 = await _create(service_client)
    assert resp2.status == 201
    data2 = await resp2.json()
    assert data2["id"] != config_id
    assert data2["version"] == 1


@pytest.mark.asyncio
async def test_get_nonexistent(service_client):
    resp = await service_client.get(
        "/api/v1/config/00000000-0000-0000-0000-000000000000",
        headers=ADMIN_HEADERS,
    )
    assert resp.status == 404


@pytest.mark.asyncio
async def test_create_missing_user(service_client):
    resp = await service_client.post("/api/v1/config", json=_BASE_PAYLOAD)
    assert resp.status == 401


@pytest.mark.asyncio
async def test_create_invalid_value(service_client):
    """Value that doesn't match feature_flag schema → 422."""
    payload = {**_BASE_PAYLOAD, "value": {"enabled": "not-a-bool"}}
    resp = await _create(service_client, payload=payload)
    assert resp.status == 422


@pytest.mark.asyncio
async def test_delete_missing_change_reason(service_client):
    resp = await _create(service_client)
    config_id = (await resp.json())["id"]
    del_resp = await service_client.delete(
        f"/api/v1/config/{config_id}?version=1",
        headers={**ADMIN_HEADERS, "If-Match": '"1"'},
    )
    assert del_resp.status == 400


@pytest.mark.asyncio
async def test_activate_deactivate(service_client):
    resp = await _create(service_client)
    data = await resp.json()
    config_id = data["id"]

    # Deactivate
    deact = await service_client.post(
        f"/api/v1/config/{config_id}/deactivate",
        json={"version": 1, "change_reason": "maintenance"},
        headers={**ADMIN_HEADERS, "If-Match": '"1"'},
    )
    assert deact.status == 200
    assert (await deact.json())["is_active"] is False

    # Activate
    act = await service_client.post(
        f"/api/v1/config/{config_id}/activate",
        json={"version": 2, "change_reason": "done"},
        headers={**ADMIN_HEADERS, "If-Match": '"2"'},
    )
    assert act.status == 200
    assert (await act.json())["is_active"] is True


@pytest.mark.asyncio
async def test_get_history(service_client):
    resp = await _create(service_client)
    config_id = (await resp.json())["id"]

    await service_client.patch(
        f"/api/v1/config/{config_id}",
        json={"version": 1, "value": {"enabled": False}, "change_reason": "v2"},
        headers={**ADMIN_HEADERS, "If-Match": '"1"'},
    )

    hist_resp = await service_client.get(
        f"/api/v1/config/{config_id}/history", headers=ADMIN_HEADERS
    )
    assert hist_resp.status == 200
    items = (await hist_resp.json())["items"]
    assert len(items) == 2
    # History is sorted newest-first (ORDER BY version DESC)
    assert items[0]["version"] == 2
    assert items[1]["version"] == 1
