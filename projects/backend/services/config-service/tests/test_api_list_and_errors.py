"""Integration tests: list filters, pagination and not-found paths."""
from __future__ import annotations

from uuid import uuid4

import pytest

from tests.config_service_test_utils import ADMIN_HEADERS


async def _create(client, *, key, service="list-service", project=None, config_type="feature_flag"):
    value = {"enabled": True} if config_type == "feature_flag" else {"rate_limit": {"requests": 10}}
    payload = {
        "service_name": service,
        "key": key,
        "config_type": config_type,
        "value": value,
    }
    if project is not None:
        payload["project_id"] = project
    resp = await client.post("/api/v1/config", json=payload, headers=ADMIN_HEADERS)
    assert resp.status == 201, await resp.text()
    return await resp.json()


async def _list(client, query):
    resp = await client.get(f"/api/v1/config?{query}", headers=ADMIN_HEADERS)
    assert resp.status == 200, await resp.text()
    return await resp.json()


@pytest.mark.asyncio
async def test_list_filters_by_project_type_and_active(service_client):
    project = f"proj-{uuid4()}"
    in_project = await _create(service_client, key="ff_in_project", project=project)
    await _create(service_client, key="ff_platform")
    inactive = await _create(service_client, key="ff_inactive", project=project)
    deact = await service_client.post(
        f"/api/v1/config/{inactive['id']}/deactivate",
        json={"version": 1, "change_reason": "off"},
        headers={**ADMIN_HEADERS, "If-Match": '"1"'},
    )
    assert deact.status == 200, await deact.text()

    data = await _list(service_client, f"project={project}&config_type=feature_flag&is_active=true")
    assert [c["id"] for c in data["items"]] == [in_project["id"]]

    data = await _list(service_client, f"project={project}&is_active=false")
    assert [c["id"] for c in data["items"]] == [inactive["id"]]


@pytest.mark.asyncio
async def test_list_paginates_with_cursor(service_client):
    project = f"proj-{uuid4()}"
    created = {(await _create(service_client, key=f"ff_page_{i}", project=project))["id"] for i in range(3)}

    seen: list[str] = []
    cursor = None
    for _ in range(4):
        query = f"project={project}&limit=2" + (f"&cursor={cursor}" if cursor else "")
        page = await _list(service_client, query)
        assert len(page["items"]) <= 2
        seen.extend(c["id"] for c in page["items"])
        cursor = page["next_cursor"]
        if cursor is None:
            break
    assert sorted(seen) == sorted(created)


@pytest.mark.asyncio
@pytest.mark.parametrize(
    "query",
    ["limit=abc", "limit=0", "cursor=not-a-uuid", "config_type=unknown"],
)
async def test_list_rejects_bad_query(service_client, query):
    resp = await service_client.get(f"/api/v1/config?{query}", headers=ADMIN_HEADERS)
    assert resp.status == 400, await resp.text()


@pytest.mark.asyncio
async def test_patch_value_of_missing_config_is_404(service_client):
    resp = await service_client.patch(
        f"/api/v1/config/{uuid4()}",
        json={"version": 1, "value": {"enabled": False}, "change_reason": "x"},
        headers={**ADMIN_HEADERS, "If-Match": '"1"'},
    )
    assert resp.status == 404, await resp.text()


@pytest.mark.asyncio
async def test_patch_flags_of_missing_config_is_404(service_client):
    resp = await service_client.patch(
        f"/api/v1/config/{uuid4()}",
        json={"version": 1, "description": "d", "change_reason": "x"},
        headers={**ADMIN_HEADERS, "If-Match": '"1"'},
    )
    assert resp.status == 404, await resp.text()


@pytest.mark.asyncio
async def test_patch_all_fields_updates_them(service_client):
    data = await _create(service_client, key="ff_all_fields")
    resp = await service_client.patch(
        f"/api/v1/config/{data['id']}",
        json={
            "version": 1,
            "change_reason": "everything",
            "description": "new description",
            "value": {"enabled": False},
            "metadata": {"owner": "team"},
            "is_active": False,
            "is_critical": True,
            "is_sensitive": False,
        },
        headers={**ADMIN_HEADERS, "If-Match": '"1"'},
    )
    assert resp.status == 200, await resp.text()
    body = await resp.json()
    assert body["description"] == "new description"
    assert body["value"] == {"enabled": False}
    assert body["metadata"] == {"owner": "team"}
    assert body["is_active"] is False
    assert body["is_critical"] is True


@pytest.mark.asyncio
async def test_rollback_with_stale_version_is_412(service_client):
    data = await _create(service_client, key="ff_rb_stale")
    for version, enabled in ((1, False), (2, True)):
        patch = await service_client.patch(
            f"/api/v1/config/{data['id']}",
            json={"version": version, "value": {"enabled": enabled}, "change_reason": "next"},
            headers={**ADMIN_HEADERS, "If-Match": f'"{version}"'},
        )
        assert patch.status == 200, await patch.text()

    # The config is at v3; a rollback sent from a stale v2 view must not apply.
    rb = await service_client.post(
        f"/api/v1/config/{data['id']}/rollback",
        json={"version": 2, "target_version": 1, "change_reason": "stale"},
        headers={**ADMIN_HEADERS, "If-Match": '"2"'},
    )
    assert rb.status == 412, await rb.text()
