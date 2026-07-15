"""Integration tests: sensitive config redaction."""
from __future__ import annotations

import pytest

from tests.config_service_test_utils import ADMIN_HEADERS, VIEWER_HEADERS, make_headers

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

_LIFECYCLE_HEADERS = make_headers(
    user_id="sensitive-operator",
    system_permissions=["configs.activate", "configs.rollback"],
)

_LIFECYCLE_SENSITIVE_READ_HEADERS = make_headers(
    user_id="sensitive-operator-reader",
    system_permissions=[
        "configs.activate",
        "configs.rollback",
        "configs.sensitive.read",
    ],
)


async def _assert_lifecycle_responses(
    service_client,
    *,
    key: str,
    headers: dict[str, str],
    is_sensitive: bool,
    expected_value: object,
) -> None:
    create_resp = await service_client.post(
        "/api/v1/config",
        json={**_SENSITIVE_PAYLOAD, "key": key, "is_sensitive": is_sensitive},
        headers=ADMIN_HEADERS,
    )
    assert create_resp.status == 201, await create_resp.text()
    config_id = (await create_resp.json())["id"]

    operations = (
        ("deactivate", 1, {"version": 1, "change_reason": "maintenance"}, False),
        ("activate", 2, {"version": 2, "change_reason": "resume"}, True),
        (
            "rollback",
            3,
            {"version": 3, "target_version": 1, "change_reason": "restore"},
            True,
        ),
    )
    for operation, current_version, payload, expected_is_active in operations:
        response = await service_client.post(
            f"/api/v1/config/{config_id}/{operation}",
            json=payload,
            headers={**headers, "If-Match": f'"{current_version}"'},
        )
        assert response.status == 200, await response.text()
        expected_version = current_version + 1
        assert response.headers.get("ETag") == f'"{expected_version}"'
        data = await response.json()
        assert data["version"] == expected_version
        assert data["is_active"] is expected_is_active
        assert data["value"] == expected_value


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


# --- lifecycle endpoint redaction -----------------------------------------


@pytest.mark.asyncio
async def test_sensitive_lifecycle_redacted_without_permission(service_client):
    await _assert_lifecycle_responses(
        service_client,
        key="lifecycle_secret_operator",
        headers=_LIFECYCLE_HEADERS,
        is_sensitive=True,
        expected_value="***",
    )


@pytest.mark.asyncio
async def test_sensitive_lifecycle_visible_with_permission(service_client):
    await _assert_lifecycle_responses(
        service_client,
        key="lifecycle_secret_reader",
        headers=_LIFECYCLE_SENSITIVE_READ_HEADERS,
        is_sensitive=True,
        expected_value={"enabled": True},
    )


@pytest.mark.asyncio
async def test_non_sensitive_lifecycle_not_redacted_without_permission(service_client):
    await _assert_lifecycle_responses(
        service_client,
        key="lifecycle_public_operator",
        headers=_LIFECYCLE_HEADERS,
        is_sensitive=False,
        expected_value={"enabled": True},
    )


# --- history endpoint redaction -------------------------------------------


@pytest.mark.asyncio
async def test_history_sensitive_redacted_for_viewer(service_client):
    create_resp = await service_client.post(
        "/api/v1/config",
        json={**_SENSITIVE_PAYLOAD, "key": "hist_secret_viewer"},
        headers=ADMIN_HEADERS,
    )
    config_id = (await create_resp.json())["id"]

    resp = await service_client.get(
        f"/api/v1/config/{config_id}/history", headers=VIEWER_HEADERS
    )
    assert resp.status == 200
    items = (await resp.json())["items"]
    assert len(items) >= 1
    for item in items:
        assert item["value"] == "***"


@pytest.mark.asyncio
async def test_history_sensitive_visible_to_superadmin(service_client):
    create_resp = await service_client.post(
        "/api/v1/config",
        json={**_SENSITIVE_PAYLOAD, "key": "hist_secret_admin"},
        headers=ADMIN_HEADERS,
    )
    config_id = (await create_resp.json())["id"]

    resp = await service_client.get(
        f"/api/v1/config/{config_id}/history", headers=ADMIN_HEADERS
    )
    assert resp.status == 200
    items = (await resp.json())["items"]
    assert len(items) >= 1
    for item in items:
        assert item["value"] == {"enabled": True}


@pytest.mark.asyncio
async def test_history_sensitive_visible_with_permission(service_client):
    create_resp = await service_client.post(
        "/api/v1/config",
        json={**_SENSITIVE_PAYLOAD, "key": "hist_secret_perm"},
        headers=ADMIN_HEADERS,
    )
    config_id = (await create_resp.json())["id"]

    resp = await service_client.get(
        f"/api/v1/config/{config_id}/history", headers=_SENSITIVE_READ_HEADERS
    )
    assert resp.status == 200
    items = (await resp.json())["items"]
    assert len(items) >= 1
    for item in items:
        assert item["value"] == {"enabled": True}


@pytest.mark.asyncio
async def test_history_non_sensitive_not_redacted(service_client):
    create_resp = await service_client.post(
        "/api/v1/config",
        json={**_SENSITIVE_PAYLOAD, "key": "hist_public_flag", "is_sensitive": False},
        headers=ADMIN_HEADERS,
    )
    config_id = (await create_resp.json())["id"]

    resp = await service_client.get(
        f"/api/v1/config/{config_id}/history", headers=VIEWER_HEADERS
    )
    assert resp.status == 200
    items = (await resp.json())["items"]
    assert len(items) >= 1
    for item in items:
        assert item["value"] == {"enabled": True}


@pytest.mark.asyncio
async def test_history_uses_sensitive_snapshot_after_true_to_false(service_client):
    create_resp = await service_client.post(
        "/api/v1/config",
        json={**_SENSITIVE_PAYLOAD, "key": "hist_sensitive_to_public"},
        headers=ADMIN_HEADERS,
    )
    assert create_resp.status == 201, await create_resp.text()
    config_id = (await create_resp.json())["id"]

    patch_resp = await service_client.patch(
        f"/api/v1/config/{config_id}",
        json={
            "version": 1,
            "is_sensitive": False,
            "change_reason": "make public",
        },
        headers={**ADMIN_HEADERS, "If-Match": '"1"'},
    )
    assert patch_resp.status == 200, await patch_resp.text()

    viewer_resp = await service_client.get(
        f"/api/v1/config/{config_id}/history", headers=VIEWER_HEADERS
    )
    assert viewer_resp.status == 200
    viewer_items = {item["version"]: item for item in (await viewer_resp.json())["items"]}
    assert viewer_items[1]["value"] == "***"
    assert viewer_items[2]["value"] == {"enabled": True}

    reader_resp = await service_client.get(
        f"/api/v1/config/{config_id}/history", headers=_SENSITIVE_READ_HEADERS
    )
    assert reader_resp.status == 200
    reader_items = (await reader_resp.json())["items"]
    assert all(item["value"] == {"enabled": True} for item in reader_items)


@pytest.mark.asyncio
async def test_history_uses_sensitive_snapshot_after_false_to_true(service_client):
    create_resp = await service_client.post(
        "/api/v1/config",
        json={
            **_SENSITIVE_PAYLOAD,
            "key": "hist_public_to_sensitive",
            "is_sensitive": False,
        },
        headers=ADMIN_HEADERS,
    )
    assert create_resp.status == 201, await create_resp.text()
    config_id = (await create_resp.json())["id"]

    patch_resp = await service_client.patch(
        f"/api/v1/config/{config_id}",
        json={
            "version": 1,
            "is_sensitive": True,
            "change_reason": "make sensitive",
        },
        headers={**ADMIN_HEADERS, "If-Match": '"1"'},
    )
    assert patch_resp.status == 200, await patch_resp.text()

    viewer_resp = await service_client.get(
        f"/api/v1/config/{config_id}/history", headers=VIEWER_HEADERS
    )
    assert viewer_resp.status == 200
    viewer_items = {item["version"]: item for item in (await viewer_resp.json())["items"]}
    assert viewer_items[1]["value"] == {"enabled": True}
    assert viewer_items[2]["value"] == "***"
