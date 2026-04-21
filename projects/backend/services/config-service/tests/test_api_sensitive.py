"""Integration tests: sensitive config redaction."""
from __future__ import annotations

import pytest

from tests.utils import ADMIN_HEADERS, VIEWER_HEADERS, make_headers

_SENSITIVE_PAYLOAD = {
    "service_name": "sens-svc",
    "key": "api_secret",
    "config_type": "feature_flag",
    "value": {"enabled": True},
    "is_sensitive": True,
}

_SENSITIVE_READ_HEADERS = make_headers(
    user_id="sensitive-reader",
    system_permissions=["configs.view", "configs.sensitive.read"],
)


@pytest.mark.asyncio
async def test_sensitive_redacted_for_viewer(service_client):
    create_resp = await service_client.post(
        "/api/v1/config", json=_SENSITIVE_PAYLOAD, headers=ADMIN_HEADERS
    )
    config_id = (await create_resp.json())["id"]

    resp = await service_client.get(
        f"/api/v1/config/{config_id}", headers=VIEWER_HEADERS
    )
    assert resp.status == 200
    data = await resp.json()
    assert data["value"] == "***"


@pytest.mark.asyncio
async def test_sensitive_visible_to_superadmin(service_client):
    create_resp = await service_client.post(
        "/api/v1/config", json=_SENSITIVE_PAYLOAD, headers=ADMIN_HEADERS
    )
    config_id = (await create_resp.json())["id"]

    resp = await service_client.get(
        f"/api/v1/config/{config_id}", headers=ADMIN_HEADERS
    )
    data = await resp.json()
    assert data["value"] != "***"
    assert data["value"] == {"enabled": True}


@pytest.mark.asyncio
async def test_sensitive_visible_with_sensitive_read_permission(service_client):
    create_resp = await service_client.post(
        "/api/v1/config", json=_SENSITIVE_PAYLOAD, headers=ADMIN_HEADERS
    )
    config_id = (await create_resp.json())["id"]

    resp = await service_client.get(
        f"/api/v1/config/{config_id}", headers=_SENSITIVE_READ_HEADERS
    )
    data = await resp.json()
    assert data["value"] == {"enabled": True}


@pytest.mark.asyncio
async def test_sensitive_redacted_in_list(service_client):
    await service_client.post(
        "/api/v1/config",
        json={**_SENSITIVE_PAYLOAD, "key": "secret_list_flag"},
        headers=ADMIN_HEADERS,
    )

    resp = await service_client.get(
        "/api/v1/config?service=sens-svc", headers=VIEWER_HEADERS
    )
    data = await resp.json()
    sensitive_items = [i for i in data["items"] if i.get("is_sensitive")]
    for item in sensitive_items:
        assert item["value"] == "***"


@pytest.mark.asyncio
async def test_non_sensitive_not_redacted_for_viewer(service_client):
    create_resp = await service_client.post(
        "/api/v1/config",
        json={**_SENSITIVE_PAYLOAD, "key": "public_flag", "is_sensitive": False},
        headers=ADMIN_HEADERS,
    )
    config_id = (await create_resp.json())["id"]

    resp = await service_client.get(
        f"/api/v1/config/{config_id}", headers=VIEWER_HEADERS
    )
    data = await resp.json()
    assert data["value"] == {"enabled": True}
