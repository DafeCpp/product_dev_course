"""Remaining service, repository and wiring branches (ProjectService, roles repo, app setup)."""
from __future__ import annotations

from types import SimpleNamespace
from uuid import uuid4

import pytest
from aiohttp import web

import auth_service.main as app_main
from auth_service.api.routes import permissions as permissions_routes
from auth_service.api.routes import users as users_routes
from auth_service.core.exceptions import ForbiddenError, NotFoundError
from auth_service.domain.models import (
    PROJECT_EDITOR_ROLE_ID,
    PROJECT_VIEWER_ROLE_ID,
    SUPERADMIN_ROLE_ID,
    ScopeType,
)
from auth_service.repositories.permissions import PermissionRepository
from auth_service.repositories.projects import ProjectRepository
from auth_service.repositories.roles import RoleRepository
from auth_service.repositories.token_families import TokenFamilyRepository
from auth_service.repositories.user_roles import UserRoleRepository
from auth_service.repositories.users import UserRepository
from auth_service.services.dependencies import get_auth_service, get_project_service
from auth_service.services.jwt import create_access_token
from auth_service.services.permission import PermissionService
from auth_service.services.projects import ProjectService

PASSWORD_HASH = "$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG"


@pytest.fixture
async def projects(pool_with_seed):
    user_roles = UserRoleRepository(pool_with_seed)
    perm = PermissionService(
        PermissionRepository(pool_with_seed), RoleRepository(pool_with_seed), user_roles
    )
    return ProjectService(
        ProjectRepository(pool_with_seed), UserRepository(pool_with_seed), user_roles, perm
    )


@pytest.fixture
async def make_user(pool_with_seed):
    async def create(name: str, *system_roles):
        user_id = await pool_with_seed.fetchval(
            "INSERT INTO users (username, email, hashed_password, password_change_required, is_active) "
            "VALUES ($1, $2, $3, false, true) RETURNING id",
            name,
            f"{name}@example.com",
            PASSWORD_HASH,
        )
        for role_id in system_roles:
            await pool_with_seed.execute(
                "INSERT INTO user_system_roles (user_id, role_id, granted_by, granted_at) "
                "VALUES ($1, $2, $1, now())",
                user_id,
                role_id,
            )
        return user_id

    return create


def bearer(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


# --- ProjectService ------------------------------------------------------------------------------


async def test_create_project_requires_an_existing_owner(projects):
    with pytest.raises(NotFoundError, match="not found"):
        await projects.create_project("Orphan", None, uuid4())


async def test_missing_projects_are_reported_as_not_found(projects, make_user):
    root = await make_user("proj-root", SUPERADMIN_ROLE_ID)
    ghost = uuid4()

    with pytest.raises(NotFoundError):
        await projects.get_project(ghost, root)
    with pytest.raises(NotFoundError):
        await projects.update_project(ghost, root, name="x")
    with pytest.raises(NotFoundError):
        await projects.delete_project(ghost, root)
    with pytest.raises(NotFoundError):
        await projects.remove_member(ghost, root, uuid4())
    with pytest.raises(NotFoundError):
        await projects.update_member_role(ghost, root, uuid4(), None, PROJECT_EDITOR_ROLE_ID)


async def test_list_all_projects_depends_on_the_role(projects, make_user):
    root = await make_user("lap-root", SUPERADMIN_ROLE_ID)
    owner = await make_user("lap-owner")
    other = await make_user("lap-other")
    mine = await projects.create_project("Mine", None, owner)
    await projects.create_project("Theirs", None, other)

    everything = await projects.list_all_projects(root)
    own_only = await projects.list_all_projects(owner)
    assert len(everything) == 2
    assert [p.id for p in own_only] == [mine.id]


async def test_owner_cannot_be_removed_or_demoted(projects, make_user):
    owner = await make_user("own-owner")
    project = await projects.create_project("Guarded", None, owner)

    with pytest.raises(ForbiddenError, match="Cannot remove project owner"):
        await projects.remove_member(project.id, owner, owner)
    with pytest.raises(ForbiddenError, match="owner's role"):
        await projects.update_member_role(project.id, owner, owner, None, PROJECT_EDITOR_ROLE_ID)


async def test_member_roles_can_be_swapped_and_removed_individually(projects, make_user):
    owner = await make_user("swap-owner")
    member = await make_user("swap-member")
    project = await projects.create_project("Swap", None, owner)
    await projects.add_member(project.id, owner, member, PROJECT_VIEWER_ROLE_ID)

    swapped = await projects.update_member_role(
        project.id, owner, member, PROJECT_VIEWER_ROLE_ID, PROJECT_EDITOR_ROLE_ID
    )
    assert swapped.role_id == PROJECT_EDITOR_ROLE_ID
    with pytest.raises(NotFoundError, match="does not hold role"):
        await projects.update_member_role(
            project.id, owner, member, PROJECT_VIEWER_ROLE_ID, PROJECT_EDITOR_ROLE_ID
        )

    # Re-applying the same role is a no-op swap that still succeeds.
    same = await projects.update_member_role(
        project.id, owner, member, PROJECT_EDITOR_ROLE_ID, PROJECT_EDITOR_ROLE_ID
    )
    assert same.role_id == PROJECT_EDITOR_ROLE_ID

    assert await projects.remove_member(
        project.id, owner, member, role_id=PROJECT_EDITOR_ROLE_ID
    )
    with pytest.raises(NotFoundError, match="not a member"):
        await projects.remove_member(project.id, owner, member)
    with pytest.raises(NotFoundError, match="not a member"):
        await projects.update_member_role(project.id, owner, member, None, PROJECT_EDITOR_ROLE_ID)


async def test_replacing_all_roles_at_once(projects, make_user):
    owner = await make_user("rep-owner")
    member = await make_user("rep-member")
    project = await projects.create_project("Replace", None, owner)
    await projects.add_member(project.id, owner, member, PROJECT_VIEWER_ROLE_ID)

    replaced = await projects.update_member_role(
        project.id, owner, member, None, PROJECT_EDITOR_ROLE_ID
    )
    assert replaced.role_id == PROJECT_EDITOR_ROLE_ID

    unchanged = await projects.update_member_role(
        project.id, owner, member, None, PROJECT_EDITOR_ROLE_ID
    )
    assert unchanged.role_id == PROJECT_EDITOR_ROLE_ID


# --- repositories ---------------------------------------------------------------------------------


async def test_role_repository_update_and_delete_edges(pool_with_seed):
    roles = RoleRepository(pool_with_seed)
    role = await roles.create("Edge case", ScopeType.SYSTEM, description="first")

    untouched = await roles.update(role.id)
    assert untouched.name == "Edge case"
    described = await roles.update(role.id, description="second")
    assert described.description == "second"

    with pytest.raises(NotFoundError):
        await roles.update(uuid4(), name="ghost")

    await roles.delete(role.id)
    with pytest.raises(NotFoundError):
        await roles.delete(role.id)


async def test_token_family_creation_failure_is_reported(pool_with_seed, make_user):
    user_id = await make_user("famfail")

    class BrokenFamilies(TokenFamilyRepository):
        async def _fetchrow(self, *args, **kwargs):
            return None

    with pytest.raises(RuntimeError, match="Failed to create token family"):
        await BrokenFamilies(pool_with_seed).create(user_id)


# --- dependency wiring ------------------------------------------------------------------------------


async def test_services_are_cached_per_request(service_client):
    request: dict = {}
    # A plain dict stands in for the request: the providers only use item access.
    first = await get_auth_service(request)  # type: ignore[arg-type]
    second = await get_auth_service(request)  # type: ignore[arg-type]
    project = await get_project_service(request)  # type: ignore[arg-type]
    assert first is second
    assert project is await get_project_service(request)  # type: ignore[arg-type]


# --- routes -------------------------------------------------------------------------------------------


async def register(client, name: str) -> tuple[str, str]:
    response = await client.post(
        "/auth/register",
        json={"username": name, "email": f"{name}@example.com", "password": "testpass123"},
    )
    assert response.status == 201
    payload = await response.json()
    return payload["access_token"], payload["user"]["id"]


async def test_user_search_reports_unexpected_failures(service_client, monkeypatch):
    token, _ = await register(service_client, "searcher")

    async def explode(*args, **kwargs):
        raise RuntimeError("database is on fire")

    monkeypatch.setattr(users_routes, "get_pool", explode)
    response = await service_client.get("/api/v1/users/search?q=ab", headers=bearer(token))
    assert response.status == 500


async def test_permission_catalog_failure_and_project_scoped_access(service_client, monkeypatch):
    token, _ = await register(service_client, "permviewer")
    other_token, other_id = await register(service_client, "permother")
    created = await service_client.post(
        "/projects", headers=bearer(other_token), json={"name": "Private"}
    )
    project_id = (await created.json())["id"]

    # Looking at somebody else's permissions inside a project needs membership.
    denied = await service_client.get(
        f"/api/v1/users/{other_id}/effective-permissions?project_id={project_id}", headers=bearer(token)
    )
    assert denied.status == 403
    own = await service_client.get(
        f"/api/v1/users/{other_id}/effective-permissions?project_id={project_id}",
        headers=bearer(other_token),
    )
    assert own.status == 200

    async def explode(*args, **kwargs):
        raise RuntimeError("database is on fire")

    monkeypatch.setattr(permissions_routes, "get_pool", explode)
    broken = await service_client.get("/api/v1/permissions", headers=bearer(token))
    assert broken.status == 500


async def test_deactivated_user_cannot_use_a_valid_token(service_client, database_url, superadmin_token):
    token, user_id = await register(service_client, "deactivated")
    update = await service_client.patch(
        f"/auth/admin/users/{user_id}", headers=bearer(superadmin_token), json={"is_active": False}
    )
    assert update.status == 200

    response = await service_client.get("/projects", headers=bearer(token))
    assert response.status == 401
    assert (await response.json())["error"] == "User not found or inactive"


async def test_forced_password_change_still_allows_the_change_endpoints(service_client):
    _, user_id = await register(service_client, "pcruser")
    token = create_access_token(user_id, password_change_required=True)

    me = await service_client.get("/auth/me", headers=bearer(token))
    other = await service_client.get("/projects", headers=bearer(token))
    assert me.status == 200
    assert other.status == 403


# --- application factory -------------------------------------------------------------------------------


def test_config_client_hooks_are_registered_when_enabled(monkeypatch):
    started = SimpleNamespace(start=lambda app: None, stop=lambda app: None)
    monkeypatch.setattr(app_main.settings, "config_client_enabled", True)
    monkeypatch.setattr(app_main, "build_qos_client", lambda url, interval: started)

    app = app_main.create_app()
    assert started.start in app.on_startup
    assert started.stop in app.on_cleanup


def test_main_runs_the_application(monkeypatch):
    calls = {}

    def fake_run_app(app, **kwargs):
        calls["app"] = app
        calls.update(kwargs)

    monkeypatch.setattr(web, "run_app", fake_run_app)
    app_main.main()

    assert isinstance(calls["app"], web.Application)
    assert calls["access_log"] is None
    assert calls["port"] == app_main.settings.port
