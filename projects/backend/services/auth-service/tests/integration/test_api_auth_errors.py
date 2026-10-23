"""Error-path tests for the /auth routes: bad input, auth failures, mapped and unexpected errors."""
from __future__ import annotations

import uuid

import pytest

from auth_service.core.exceptions import ForbiddenError, InvalidTokenError
from auth_service.services.auth import AuthService
from auth_service.settings import settings

USER_ID = str(uuid.uuid4())
GOOD_PASSWORD = "testpass123"

# (id, http method, path, json body, AuthService method the handler calls, needs a bearer token)
ROUTES = [
    ("register", "POST", "/auth/register",
     {"username": "someone", "email": "someone@example.com", "password": GOOD_PASSWORD},
     "register", False),
    ("login", "POST", "/auth/login", {"username": "someone", "password": GOOD_PASSWORD},
     "login", False),
    ("refresh", "POST", "/auth/refresh", {"refresh_token": "x"}, "refresh_token", False),
    ("logout", "POST", "/auth/logout", {"refresh_token": "x"}, "logout", False),
    ("me", "GET", "/auth/me", None, "get_user_by_token", True),
    ("change-password", "POST", "/auth/change-password",
     {"old_password": GOOD_PASSWORD, "new_password": "another-pass1"},
     "change_password", True),
    ("reset-confirm", "POST", "/auth/password-reset/confirm",
     {"reset_token": "x", "new_password": "another-pass1"}, "confirm_password_reset", False),
    ("admin-reset", "POST", f"/auth/admin/users/{USER_ID}/reset",
     {"new_password": "another-pass1"}, "admin_reset_user", True),
    ("create-invite", "POST", "/auth/admin/invites", {}, "create_invite", True),
    ("list-invites", "GET", "/auth/admin/invites", None, "list_invites", True),
    ("revoke-invite", "DELETE", f"/auth/admin/invites/{USER_ID}", None, "revoke_invite", True),
    ("list-users", "GET", "/auth/admin/users", None, "list_users", True),
    ("update-user", "PATCH", f"/auth/admin/users/{USER_ID}", {"is_active": False},
     "update_user", True),
    ("delete-user", "DELETE", f"/auth/admin/users/{USER_ID}", None, "delete_user", True),
]
IDS = [r[0] for r in ROUTES]


async def register_token(client, name: str = "authuser") -> str:
    response = await client.post(
        "/auth/register",
        json={"username": name, "email": f"{name}@example.com", "password": GOOD_PASSWORD},
    )
    assert response.status == 201
    return (await response.json())["access_token"]


def bearer(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


@pytest.mark.parametrize("name,method,path,body,action,needs_token", ROUTES, ids=IDS)
async def test_unexpected_error_returns_500(
    service_client, monkeypatch, name, method, path, body, action, needs_token
):
    token = await register_token(service_client) if needs_token else None

    async def explode(self, *args, **kwargs):
        raise RuntimeError("database is on fire")

    monkeypatch.setattr(AuthService, action, explode)
    response = await service_client.request(
        method, path, json=body, headers=bearer(token) if token else None
    )
    assert response.status == 500
    assert (await response.json())["error"] == "Internal server error"


@pytest.mark.parametrize("name,method,path,body,action,needs_token", ROUTES, ids=IDS)
async def test_domain_error_is_mapped_to_its_status(
    service_client, monkeypatch, name, method, path, body, action, needs_token
):
    token = await register_token(service_client) if needs_token else None

    async def deny(self, *args, **kwargs):
        raise ForbiddenError("nope")

    monkeypatch.setattr(AuthService, action, deny)
    response = await service_client.request(
        method, path, json=body, headers=bearer(token) if token else None
    )
    assert response.status == 403
    assert (await response.json())["error"] == "nope"


@pytest.mark.parametrize(
    "method,path",
    [
        ("POST", "/auth/register"),
        ("POST", "/auth/login"),
        ("POST", "/auth/refresh"),
        ("POST", "/auth/logout"),
        ("POST", "/auth/change-password"),
        ("POST", "/auth/password-reset/request"),
        ("POST", "/auth/password-reset/confirm"),
        ("POST", f"/auth/admin/users/{USER_ID}/reset"),
        ("POST", "/auth/admin/invites"),
        ("PATCH", f"/auth/admin/users/{USER_ID}"),
    ],
)
async def test_malformed_body_is_rejected(service_client, method, path):
    token = await register_token(service_client)
    response = await service_client.request(
        method, path, headers=bearer(token), data="not json"
    )
    assert response.status == 400


@pytest.mark.parametrize(
    "method,path",
    [
        ("DELETE", "/auth/admin/invites/not-a-uuid"),
        ("DELETE", "/auth/admin/users/not-a-uuid"),
        ("PATCH", "/auth/admin/users/not-a-uuid"),
    ],
)
async def test_invalid_uuid_in_path_is_rejected(service_client, method, path):
    token = await register_token(service_client)
    response = await service_client.request(
        method, path, headers=bearer(token), json={"is_active": True}
    )
    assert response.status == 400


async def test_admin_reset_rejects_invalid_user_id(service_client):
    token = await register_token(service_client)
    response = await service_client.post(
        "/auth/admin/users/not-a-uuid/reset",
        headers=bearer(token),
        json={"new_password": "another-pass1"},
    )
    assert response.status == 400


@pytest.mark.parametrize(
    "method,path",
    [
        ("GET", "/auth/me"),
        ("POST", "/auth/change-password"),
        ("POST", f"/auth/admin/users/{USER_ID}/reset"),
        ("POST", "/auth/admin/invites"),
        ("GET", "/auth/admin/invites"),
        ("DELETE", f"/auth/admin/invites/{USER_ID}"),
        ("GET", "/auth/admin/users"),
        ("PATCH", f"/auth/admin/users/{USER_ID}"),
        ("DELETE", f"/auth/admin/users/{USER_ID}"),
    ],
)
async def test_protected_routes_require_a_token(service_client, method, path):
    body = {
        "old_password": GOOD_PASSWORD,
        "new_password": "another-pass1",
        "is_active": True,
    }
    response = await service_client.request(method, path, json=body)
    assert response.status == 401


async def test_protected_routes_reject_garbage_token(service_client):
    response = await service_client.get("/auth/me", headers=bearer("garbage"))
    assert response.status == 401


async def test_regular_user_cannot_use_admin_routes(service_client):
    token = await register_token(service_client, "plainuser")
    headers = bearer(token)

    listed = await service_client.get("/auth/admin/users", headers=headers)
    invites = await service_client.get("/auth/admin/invites", headers=headers)
    created = await service_client.post("/auth/admin/invites", headers=headers, json={})
    deleted = await service_client.delete(f"/auth/admin/users/{USER_ID}", headers=headers)
    assert [listed.status, invites.status, created.status, deleted.status] == [403] * 4


async def test_list_users_filters_by_active_flag(service_client, superadmin_token):
    await register_token(service_client, "activeone")
    headers = bearer(superadmin_token)

    active = await service_client.get("/auth/admin/users?is_active=true", headers=headers)
    inactive = await service_client.get("/auth/admin/users?is_active=false", headers=headers)
    assert active.status == 200
    assert inactive.status == 200
    assert all(u["is_active"] for u in await active.json())
    assert not any(u["is_active"] for u in await inactive.json())


async def test_list_invites_active_only_flag(service_client, superadmin_token):
    headers = bearer(superadmin_token)
    created = await service_client.post(
        "/auth/admin/invites", headers=headers, json={"email_hint": "new@example.com"}
    )
    assert created.status == 201

    everything = await service_client.get("/auth/admin/invites", headers=headers)
    active = await service_client.get("/auth/admin/invites?active_only=1", headers=headers)
    assert everything.status == 200
    assert active.status == 200
    assert len(await active.json()) == 1


async def test_password_reset_request_never_reveals_failures(service_client, monkeypatch):
    async def deny(self, *args, **kwargs):
        raise InvalidTokenError("unknown email")

    async def explode(self, *args, **kwargs):
        raise RuntimeError("smtp down")

    body = {"email": "ghost@example.com"}
    unknown = await service_client.post("/auth/password-reset/request", json=body)
    assert unknown.status == 200

    monkeypatch.setattr(AuthService, "request_password_reset", deny)
    denied = await service_client.post("/auth/password-reset/request", json=body)
    assert denied.status == 200

    monkeypatch.setattr(AuthService, "request_password_reset", explode)
    broken = await service_client.post("/auth/password-reset/request", json=body)
    assert broken.status == 200
    assert (await broken.json()) == (await unknown.json())


async def test_bootstrap_is_disabled_without_secret(service_client, monkeypatch):
    monkeypatch.setattr(settings, "admin_bootstrap_secret", "")
    response = await service_client.post("/auth/admin/bootstrap", json={})
    assert response.status == 404


@pytest.mark.parametrize("payload,status", [({}, 400), ({"bootstrap_secret": "wrong"}, 400)])
async def test_bootstrap_validates_input(service_client, monkeypatch, payload, status):
    monkeypatch.setattr(settings, "admin_bootstrap_secret", "s3cret-value")
    response = await service_client.post("/auth/admin/bootstrap", json=payload)
    assert response.status == status


async def test_bootstrap_rejects_wrong_secret_and_reports_errors(service_client, monkeypatch):
    monkeypatch.setattr(settings, "admin_bootstrap_secret", "s3cret-value")
    body = {
        "bootstrap_secret": "wrong-secret",
        "username": "rootuser",
        "email": "root@example.com",
        "password": GOOD_PASSWORD,
    }
    rejected = await service_client.post("/auth/admin/bootstrap", json=body)
    assert rejected.status in (401, 403)

    async def explode(self, *args, **kwargs):
        raise RuntimeError("database is on fire")

    monkeypatch.setattr(AuthService, "bootstrap_admin", explode)
    broken = await service_client.post("/auth/admin/bootstrap", json=body)
    assert broken.status == 500


async def test_bootstrap_creates_first_superadmin(service_client, monkeypatch):
    monkeypatch.setattr(settings, "admin_bootstrap_secret", "s3cret-value")
    body = {
        "bootstrap_secret": "s3cret-value",
        "username": "rootuser",
        "email": "root@example.com",
        "password": GOOD_PASSWORD,
    }
    created = await service_client.post("/auth/admin/bootstrap", json=body)
    assert created.status == 201
    payload = await created.json()
    assert payload["user"]["username"] == "rootuser"
    assert payload["access_token"]

    again = await service_client.post("/auth/admin/bootstrap", json=body)
    assert again.status in (403, 409)


async def test_login_flags_password_change_required(service_client, superadmin_token):
    headers = bearer(superadmin_token)
    await register_token(service_client, "mustchange")
    users = await (await service_client.get("/auth/admin/users", headers=headers)).json()
    target = next(u for u in users if u["username"] == "mustchange")

    reset = await service_client.post(
        f"/auth/admin/users/{target['id']}/reset",
        headers=headers,
        json={"new_password": "another-pass1"},
    )
    assert reset.status == 200
    assert (await reset.json())["password_change_required"] is True

    login = await service_client.post(
        "/auth/login", json={"username": "mustchange", "password": "another-pass1"}
    )
    assert login.status == 200
    payload = await login.json()
    assert payload["password_change_required"] is True

    # The token only allows the password-change flow: everything else is walled off.
    blocked = await service_client.get("/projects", headers=bearer(payload["access_token"]))
    assert blocked.status == 403
    assert (await blocked.json())["error"] == "password_change_required"
