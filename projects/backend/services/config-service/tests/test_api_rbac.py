"""Integration tests: RBAC permission matrix."""
from __future__ import annotations

import pytest

from tests.utils import ADMIN_HEADERS, EDITOR_HEADERS, VIEWER_HEADERS, make_headers

_PAYLOAD = {
    "service_name": "rbac-svc",
    "key": "rbac_flag",
    "config_type": "feature_flag",
    "value": {"enabled": True},
}


async def _create(client, headers=None):
    resp = await client.post(
        "/api/v1/config",
        json={**_PAYLOAD, "key": f"rbac_{id(client)}_flag"},
        headers=headers or ADMIN_HEADERS,
    )
    return resp


@pytest.mark.asyncio
async def test_viewer_can_list(service_client):
    resp = await service_client.get(
        "/api/v1/config?service=rbac-svc", headers=VIEWER_HEADERS
    )
    assert resp.status == 200


@pytest.mark.asyncio
async def test_viewer_cannot_create(service_client):
    resp = await service_client.post(
        "/api/v1/config", json=_PAYLOAD, headers=VIEWER_HEADERS
    )
    assert resp.status == 403


@pytest.mark.asyncio
async def test_viewer_cannot_patch(service_client):
    create_resp = await _create(service_client)
    config_id = (await create_resp.json())["id"]

    resp = await service_client.patch(
        f"/api/v1/config/{config_id}",
        json={"version": 1, "value": {"enabled": False}, "change_reason": "test"},
        headers={**VIEWER_HEADERS, "If-Match": '"1"'},
    )
    assert resp.status == 403


@pytest.mark.asyncio
async def test_viewer_cannot_delete(service_client):
    create_resp = await _create(service_client)
    config_id = (await create_resp.json())["id"]

    resp = await service_client.delete(
        f"/api/v1/config/{config_id}?version=1&change_reason=cleanup",
        headers={**VIEWER_HEADERS, "If-Match": '"1"'},
    )
    assert resp.status == 403


@pytest.mark.asyncio
async def test_editor_can_create(service_client):
    resp = await service_client.post(
        "/api/v1/config",
        json={**_PAYLOAD, "key": "editor_created_flag"},
        headers=EDITOR_HEADERS,
    )
    assert resp.status == 201


@pytest.mark.asyncio
async def test_editor_can_patch(service_client):
    create_resp = await _create(service_client)
    config_id = (await create_resp.json())["id"]

    resp = await service_client.patch(
        f"/api/v1/config/{config_id}",
        json={"version": 1, "value": {"enabled": False}, "change_reason": "edit"},
        headers={**EDITOR_HEADERS, "If-Match": '"1"'},
    )
    assert resp.status == 200


@pytest.mark.asyncio
async def test_editor_cannot_manage_schemas(service_client):
    current = await (
        await service_client.get("/api/v1/schemas/feature_flag", headers=ADMIN_HEADERS)
    ).json()

    resp = await service_client.put(
        "/api/v1/schemas/feature_flag",
        json={"schema": current["schema"]},
        headers=EDITOR_HEADERS,
    )
    assert resp.status == 403


@pytest.mark.asyncio
async def test_unauthenticated_returns_401(service_client):
    for endpoint, method in [
        ("/api/v1/config", "get"),
        ("/api/v1/schemas", "get"),
    ]:
        resp = await getattr(service_client, method)(endpoint)
        assert resp.status == 401, f"{method.upper()} {endpoint} should be 401"


@pytest.mark.asyncio
async def test_superadmin_has_full_access(service_client):
    superadmin = make_headers(user_id="super", is_superadmin=True)
    resp = await service_client.post(
        "/api/v1/config",
        json={**_PAYLOAD, "key": "superadmin_flag"},
        headers=superadmin,
    )
    assert resp.status == 201
