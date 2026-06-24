"""Integration tests: RBAC matrix for config-service endpoints.

Verifies the fine-grained permission gates that back the built-in roles
defined in auth-service (config_viewer / config_editor / config_operator /
config_admin, see migration 003_config_rbac.sql). The «4-eyes» principle is
enforced at the permission level: an editor can write drafts but not activate
them; an operator can activate/rollback but not edit.

Permissions are delivered to the service via the trusted
``X-User-System-Permissions`` header (injected by auth-proxy from the user's
effective permissions).
"""
from __future__ import annotations

import pytest

from tests.utils import ADMIN_HEADERS, make_headers

# Role → permission bundles (mirror of migration 003_config_rbac.sql).
VIEWER = make_headers(user_id="viewer", system_permissions=["configs.view"])
EDITOR = make_headers(
    user_id="editor",
    system_permissions=["configs.view", "configs.create", "configs.update", "configs.delete"],
)
OPERATOR = make_headers(
    user_id="operator",
    system_permissions=["configs.view", "configs.activate", "configs.rollback"],
)
CONFIG_ADMIN = make_headers(
    user_id="cfgadmin",
    system_permissions=[
        "configs.view",
        "configs.create",
        "configs.update",
        "configs.delete",
        "configs.activate",
        "configs.rollback",
        "configs.schemas.manage",
        "configs.sensitive.read",
    ],
)
NO_PERMS = make_headers(user_id="nobody")

_FF_PAYLOAD = {
    "service_name": "rbac-svc",
    "key": "rbac_flag",
    "config_type": "feature_flag",
    "value": {"enabled": True},
}


async def _create(client, key: str, headers=ADMIN_HEADERS):
    """Create a feature_flag config as superadmin (setup helper)."""
    resp = await client.post(
        "/api/v1/config", json={**_FF_PAYLOAD, "key": key}, headers=headers
    )
    assert resp.status == 201, await resp.text()
    return await resp.json()


# ── viewer ──────────────────────────────────────────────────────────────────


@pytest.mark.asyncio
async def test_viewer_can_read(service_client):
    await _create(service_client, "viewer_read")
    assert (await service_client.get("/api/v1/config?service=rbac-svc", headers=VIEWER)).status == 200
    assert (await service_client.get("/api/v1/schemas", headers=VIEWER)).status == 200


@pytest.mark.asyncio
async def test_viewer_cannot_write_or_publish(service_client):
    data = await _create(service_client, "viewer_nowrite")
    cid = data["id"]

    # create
    r = await service_client.post(
        "/api/v1/config", json={**_FF_PAYLOAD, "key": "x"}, headers=VIEWER
    )
    assert r.status == 403
    # update
    r = await service_client.patch(
        f"/api/v1/config/{cid}",
        json={"version": 1, "value": {"enabled": False}},
        headers={**VIEWER, "If-Match": '"1"'},
    )
    assert r.status == 403
    # activate
    r = await service_client.post(
        f"/api/v1/config/{cid}/activate",
        json={"version": 1},
        headers={**VIEWER, "If-Match": '"1"'},
    )
    assert r.status == 403


# ── editor ──────────────────────────────────────────────────────────────────


@pytest.mark.asyncio
async def test_editor_can_create_update_delete(service_client):
    # create
    r = await service_client.post(
        "/api/v1/config", json={**_FF_PAYLOAD, "key": "editor_create"}, headers=EDITOR
    )
    assert r.status == 201, await r.text()
    cid = (await r.json())["id"]

    # update
    r = await service_client.patch(
        f"/api/v1/config/{cid}",
        json={"version": 1, "value": {"enabled": False}, "change_reason": "edit"},
        headers={**EDITOR, "If-Match": '"1"'},
    )
    assert r.status == 200, await r.text()

    # dry-run (gated by configs.update)
    r = await service_client.patch(
        f"/api/v1/config/{cid}?dry_run=true",
        json={"version": 2, "value": {"enabled": True}, "change_reason": "preview"},
        headers={**EDITOR, "If-Match": '"2"'},
    )
    assert r.status == 200, await r.text()

    # delete
    r = await service_client.delete(
        f"/api/v1/config/{cid}?version=2&change_reason=cleanup",
        headers={**EDITOR, "If-Match": '"2"'},
    )
    assert r.status == 204, await r.text()


@pytest.mark.asyncio
async def test_editor_cannot_activate_or_rollback(service_client):
    data = await _create(service_client, "editor_noactivate")
    cid = data["id"]

    r = await service_client.post(
        f"/api/v1/config/{cid}/activate",
        json={"version": 1},
        headers={**EDITOR, "If-Match": '"1"'},
    )
    assert r.status == 403
    r = await service_client.post(
        f"/api/v1/config/{cid}/deactivate",
        json={"version": 1},
        headers={**EDITOR, "If-Match": '"1"'},
    )
    assert r.status == 403
    r = await service_client.post(
        f"/api/v1/config/{cid}/rollback",
        json={"version": 1, "target_version": 1},
        headers={**EDITOR, "If-Match": '"1"'},
    )
    assert r.status == 403


# ── operator ────────────────────────────────────────────────────────────────


@pytest.mark.asyncio
async def test_operator_can_activate_and_rollback(service_client):
    data = await _create(service_client, "op_activate")
    cid = data["id"]

    # activate (v1 → v2)
    r = await service_client.post(
        f"/api/v1/config/{cid}/activate",
        json={"version": 1, "change_reason": "go live"},
        headers={**OPERATOR, "If-Match": '"1"'},
    )
    assert r.status == 200, await r.text()

    # deactivate (v2 → v3) — also gated by configs.activate
    r = await service_client.post(
        f"/api/v1/config/{cid}/deactivate",
        json={"version": 2, "change_reason": "pause"},
        headers={**OPERATOR, "If-Match": '"2"'},
    )
    assert r.status == 200, await r.text()

    # rollback (v3 → v1) becomes v4
    r = await service_client.post(
        f"/api/v1/config/{cid}/rollback",
        json={"version": 3, "target_version": 1, "change_reason": "revert"},
        headers={**OPERATOR, "If-Match": '"3"'},
    )
    assert r.status == 200, await r.text()


@pytest.mark.asyncio
async def test_operator_cannot_edit(service_client):
    data = await _create(service_client, "op_noedit")
    cid = data["id"]

    r = await service_client.post(
        "/api/v1/config", json={**_FF_PAYLOAD, "key": "y"}, headers=OPERATOR
    )
    assert r.status == 403
    r = await service_client.patch(
        f"/api/v1/config/{cid}",
        json={"version": 1, "value": {"enabled": False}},
        headers={**OPERATOR, "If-Match": '"1"'},
    )
    assert r.status == 403
    r = await service_client.delete(
        f"/api/v1/config/{cid}?version=1&change_reason=x", headers=OPERATOR
    )
    assert r.status == 403


# ── admin ───────────────────────────────────────────────────────────────────


@pytest.mark.asyncio
async def test_config_admin_can_manage_schema(service_client):
    schema = {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "type": "object",
        "required": ["enabled"],
        "properties": {"enabled": {"type": "boolean"}, "ratio": {"type": "number"}},
        "additionalProperties": False,
    }
    r = await service_client.put(
        "/api/v1/schemas/feature_flag",
        json={"schema": schema},
        headers=CONFIG_ADMIN,
    )
    assert r.status == 200, await r.text()


@pytest.mark.asyncio
async def test_editor_cannot_manage_schema(service_client):
    r = await service_client.put(
        "/api/v1/schemas/feature_flag",
        json={"schema": {"type": "object"}},
        headers=EDITOR,
    )
    assert r.status == 403


# ── superadmin bypass + gap regression ──────────────────────────────────────


@pytest.mark.asyncio
async def test_superadmin_bypasses_all_checks(service_client):
    # ADMIN_HEADERS is X-User-Is-Superadmin: true — no explicit permissions.
    r = await service_client.put(
        "/api/v1/schemas/feature_flag",
        json={
            "schema": {
                "$schema": "https://json-schema.org/draft/2020-12/schema",
                "type": "object",
                "required": ["enabled"],
                "properties": {"enabled": {"type": "boolean"}},
                "additionalProperties": False,
            }
        },
        headers=ADMIN_HEADERS,
    )
    assert r.status == 200, await r.text()


@pytest.mark.asyncio
async def test_schema_read_requires_view_permission(service_client):
    """Regression: schema GET endpoints used to be ungated (any authed user)."""
    assert (await service_client.get("/api/v1/schemas", headers=NO_PERMS)).status == 403
    assert (
        await service_client.get("/api/v1/schemas/feature_flag", headers=NO_PERMS)
    ).status == 403
    assert (
        await service_client.get("/api/v1/schemas/feature_flag/history", headers=NO_PERMS)
    ).status == 403
