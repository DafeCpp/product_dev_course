"""Error mapping for the system-role and project-role routes."""
from __future__ import annotations

import uuid

import pytest

from auth_service.api.routes import project_roles, system_roles
from auth_service.domain.models import ADMIN_ROLE_ID, PROJECT_OWNER_ROLE_ID

SOME_ID = str(uuid.uuid4())
PROJECT_ROLES = "/api/v1/projects/{pid}/roles"
MEMBER_ROLES = "/api/v1/projects/{pid}/members/{uid}/roles"

SYSTEM_ENDPOINTS = [
    ("GET", "/api/v1/system-roles", None),
    ("GET", f"/api/v1/system-roles/{SOME_ID}", None),
    ("POST", "/api/v1/system-roles", {"name": "x", "permissions": []}),
    ("PATCH", f"/api/v1/system-roles/{SOME_ID}", {"name": "y"}),
    ("DELETE", f"/api/v1/system-roles/{SOME_ID}", None),
    ("GET", f"/api/v1/users/{SOME_ID}/system-roles", None),
    ("POST", f"/api/v1/users/{SOME_ID}/system-roles", {"role_id": SOME_ID}),
    ("DELETE", f"/api/v1/users/{SOME_ID}/system-roles/{SOME_ID}", None),
]
PROJECT_ENDPOINTS = [
    ("GET", f"/api/v1/projects/{SOME_ID}/roles", None),
    ("GET", f"/api/v1/projects/{SOME_ID}/roles/{SOME_ID}", None),
    ("POST", f"/api/v1/projects/{SOME_ID}/roles", {"name": "x", "permissions": []}),
    ("PATCH", f"/api/v1/projects/{SOME_ID}/roles/{SOME_ID}", {"name": "y"}),
    ("DELETE", f"/api/v1/projects/{SOME_ID}/roles/{SOME_ID}", None),
    ("GET", f"/api/v1/projects/{SOME_ID}/members/{SOME_ID}/roles", None),
    ("POST", f"/api/v1/projects/{SOME_ID}/members/{SOME_ID}/roles", {"role_id": SOME_ID}),
    ("DELETE", f"/api/v1/projects/{SOME_ID}/members/{SOME_ID}/roles/{SOME_ID}", None),
]


def label(endpoint) -> str:
    return f"{endpoint[0]} {endpoint[1].replace(SOME_ID, '{id}')}"


def bearer(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


async def register(client, name: str) -> tuple[str, str]:
    response = await client.post(
        "/auth/register",
        json={"username": name, "email": f"{name}@example.com", "password": "testpass123"},
    )
    assert response.status == 201
    payload = await response.json()
    return payload["access_token"], payload["user"]["id"]


@pytest.mark.parametrize("endpoint", SYSTEM_ENDPOINTS, ids=label)
async def test_system_role_routes_report_unexpected_failures(service_client, monkeypatch, endpoint):
    async def explode(*args, **kwargs):
        raise RuntimeError("database is on fire")

    monkeypatch.setattr(system_roles, "get_pool", explode)
    monkeypatch.setattr(system_roles, "get_permission_service", explode)

    method, path, body = endpoint
    response = await service_client.request(method, path, json=body)
    assert response.status == 500
    assert (await response.json())["error"] == "database is on fire"


@pytest.mark.parametrize("endpoint", PROJECT_ENDPOINTS, ids=label)
async def test_project_role_routes_report_unexpected_failures(service_client, monkeypatch, endpoint):
    async def explode(*args, **kwargs):
        raise RuntimeError("database is on fire")

    monkeypatch.setattr(project_roles, "get_pool", explode)
    monkeypatch.setattr(project_roles, "get_permission_service", explode)

    method, path, body = endpoint
    response = await service_client.request(method, path, json=body)
    assert response.status == 500
    assert (await response.json())["error"] == "database is on fire"


# Reading one system role by id is public (role definitions are not secret), so it is excluded.
PROTECTED = [e for e in SYSTEM_ENDPOINTS + PROJECT_ENDPOINTS if e[1] != f"/api/v1/system-roles/{SOME_ID}" or e[0] != "GET"]


@pytest.mark.parametrize("endpoint", PROTECTED, ids=label)
async def test_role_routes_require_authentication(service_client, endpoint):
    method, path, body = endpoint
    response = await service_client.request(method, path, json=body)
    assert response.status in (401, 403)


async def test_project_role_must_belong_to_the_project(service_client):
    token, _ = await register(service_client, "rolechecker")
    created = await service_client.post(
        "/projects", headers=bearer(token), json={"name": "Roles"}
    )
    project_id = (await created.json())["id"]

    foreign = await service_client.get(
        f"/api/v1/projects/{project_id}/roles/{ADMIN_ROLE_ID}", headers=bearer(token)
    )
    assert foreign.status == 404
    assert "not found in this project" in (await foreign.json())["error"]


async def test_project_role_is_not_a_system_role(service_client, superadmin_token):
    response = await service_client.get(
        f"/api/v1/system-roles/{PROJECT_OWNER_ROLE_ID}", headers=bearer(superadmin_token)
    )
    assert response.status == 404
    assert (await response.json())["error"] == "Not a system role"


async def test_grant_project_role_requires_role_id(service_client):
    token, _ = await register(service_client, "grantnorole")
    created = await service_client.post("/projects", headers=bearer(token), json={"name": "G"})
    project_id = (await created.json())["id"]

    response = await service_client.post(
        f"/api/v1/projects/{project_id}/members/{SOME_ID}/roles",
        headers=bearer(token),
        json={},
    )
    assert response.status == 400
    assert (await response.json())["error"] == "role_id is required"


async def test_system_role_grant_accepts_expiry_and_reports_revoke_of_missing(
    service_client, superadmin_token
):
    _, user_id = await register(service_client, "expiring")
    headers = bearer(superadmin_token)

    granted = await service_client.post(
        f"/api/v1/users/{user_id}/system-roles",
        headers=headers,
        json={"role_id": str(ADMIN_ROLE_ID), "expires_at": "2099-01-01T00:00:00Z"},
    )
    assert granted.status == 201
    assert (await granted.json())["expires_at"].startswith("2099-01-01")

    listed = await service_client.get(f"/api/v1/users/{user_id}/system-roles", headers=headers)
    assert [r["role_id"] for r in await listed.json()] == [str(ADMIN_ROLE_ID)]

    revoked = await service_client.delete(
        f"/api/v1/users/{user_id}/system-roles/{ADMIN_ROLE_ID}", headers=headers
    )
    assert revoked.status == 200
    again = await service_client.delete(
        f"/api/v1/users/{user_id}/system-roles/{ADMIN_ROLE_ID}", headers=headers
    )
    assert again.status == 404
