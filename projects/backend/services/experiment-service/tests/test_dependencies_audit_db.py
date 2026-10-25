"""Request-scoped dependencies and the audit client, without going through the HTTP layer."""
from __future__ import annotations

import asyncio
import uuid
from types import SimpleNamespace
from typing import Self

import pytest
from aiohttp import web

from experiment_service.services import dependencies
from experiment_service.services.audit_client import AuditClient

SERVICE_GETTERS = sorted(
    name for name in dir(dependencies) if name.startswith("get_") and name.endswith("_service")
)


class FakeRequest(dict):
    """Minimal stand-in: the getters only cache into the request mapping."""


class FakeResponse:
    def __init__(self, status: int) -> None:
        self.status = status

    async def __aenter__(self) -> Self:
        return self

    async def __aexit__(self, *exc_info) -> None:
        return None

    async def text(self) -> str:
        return "denied"


class FakeSession:
    def __init__(self, status: int = 200, error: Exception | None = None) -> None:
        self.status = status
        self.error = error
        self.calls: list[dict] = []

    def post(self, url, json=None, timeout=None):
        if self.error is not None:
            raise self.error
        self.calls.append({"url": url, "json": json})
        return FakeResponse(self.status)


def headers_request(headers: dict[str, str]) -> SimpleNamespace:
    return SimpleNamespace(headers=headers)


# --- service getters --------------------------------------------------------------------------------------


@pytest.mark.parametrize("getter_name", SERVICE_GETTERS)
async def test_service_getters_build_and_cache(service_client, getter_name):
    getter = getattr(dependencies, getter_name)
    request = FakeRequest()
    first = await getter(request)
    second = await getter(request)
    assert first is not None
    assert first is second


# --- user context and permissions -------------------------------------------------------------------------


async def test_require_current_user_rejects_bad_headers():
    with pytest.raises(web.HTTPUnauthorized):
        await dependencies.require_current_user(headers_request({}))
    with pytest.raises(web.HTTPBadRequest):
        await dependencies.require_current_user(headers_request({dependencies.USER_ID_HEADER: "nope"}))
    with pytest.raises(web.HTTPBadRequest):
        await dependencies.require_current_user(
            headers_request(
                {dependencies.USER_ID_HEADER: str(uuid.uuid4()), dependencies.PROJECT_ID_HEADER: "nope"}
            )
        )


def make_user(**extra) -> dependencies.UserContext:
    values = {
        "user_id": uuid.uuid4(),
        "is_superadmin": False,
        "system_permissions": frozenset(),
        "project_permissions": frozenset(),
        "active_project_id": None,
    }
    values.update(extra)
    return dependencies.UserContext(**values)


def test_infer_project_role():
    assert dependencies.infer_project_role(make_user(is_superadmin=True)) == "owner"
    assert dependencies.infer_project_role(make_user(project_permissions=frozenset({"project.roles.manage"}))) == "owner"
    assert dependencies.infer_project_role(make_user(system_permissions=frozenset({"runs.create"}))) == "editor"
    assert dependencies.infer_project_role(make_user(project_permissions=frozenset({"experiments.create"}))) == "editor"
    assert dependencies.infer_project_role(make_user(project_permissions=frozenset({"experiments.view"}))) == "viewer"


def test_ensure_project_access():
    project_id = uuid.uuid4()
    dependencies.ensure_project_access(make_user(is_superadmin=True), project_id)
    dependencies.ensure_project_access(make_user(project_permissions=frozenset({"experiments.view"})), project_id)
    dependencies.ensure_project_access(make_user(system_permissions=frozenset({"experiments.view"})), project_id)
    with pytest.raises(web.HTTPForbidden):
        dependencies.ensure_project_access(make_user(), project_id)


def test_ensure_permission_accepts_system_permissions():
    user = make_user(system_permissions=frozenset({"experiments.view"}))
    dependencies.ensure_permission(user, "experiments.view")
    with pytest.raises(web.HTTPForbidden):
        dependencies.ensure_permission(user, "experiments.delete")


# --- audit client -----------------------------------------------------------------------------------------


async def test_audit_client_sends_all_fields(monkeypatch):
    client = AuditClient("http://auth:8001", FakeSession())
    sent: list[dict] = []

    async def record(payload):
        sent.append(payload)

    monkeypatch.setattr(client, "_send", record)
    scope_id = uuid.uuid4()
    client.log_action(
        uuid.uuid4(),
        "run.create",
        "project",
        scope_id=scope_id,
        target_type="run",
        target_id="r1",
        details={"k": "v"},
        ip_address="10.0.0.1",
        user_agent="pytest",
    )
    client.log_action(uuid.uuid4(), "run.delete", "project")
    for _ in range(3):
        await asyncio.sleep(0)

    full, minimal = sent
    assert full["scope_id"] == str(scope_id) and full["details"] == {"k": "v"}
    assert (full["ip_address"], full["user_agent"], full["target_type"], full["target_id"]) == (
        "10.0.0.1",
        "pytest",
        "run",
        "r1",
    )
    assert set(minimal) == {"actor_id", "action", "scope_type"}


async def test_audit_client_send_tolerates_failures():
    payload = {"action": "run.create"}
    ok = FakeSession()
    await AuditClient("http://auth:8001", ok)._send(payload)
    assert ok.calls and ok.calls[0]["url"].endswith("/api/v1/internal/audit")

    await AuditClient("http://auth:8001", FakeSession(status=500))._send(payload)
    await AuditClient("http://auth:8001", FakeSession(error=RuntimeError("down")))._send(payload)
