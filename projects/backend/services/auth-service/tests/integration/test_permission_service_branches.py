"""PermissionService branches not covered by the happy-path RBAC tests."""
from __future__ import annotations

from uuid import UUID, uuid4

import asyncpg
import pytest

from auth_service.core.exceptions import ConflictError, ForbiddenError, NotFoundError
from auth_service.domain.models import (
    ADMIN_ROLE_ID,
    PROJECT_EDITOR_ROLE_ID,
    PROJECT_OWNER_ROLE_ID,
    PROJECT_VIEWER_ROLE_ID,
    SUPERADMIN_ROLE_ID,
    ScopeType,
)
from auth_service.repositories.audit import AuditRepository
from auth_service.repositories.permissions import PermissionRepository
from auth_service.repositories.roles import RoleRepository
from auth_service.repositories.user_roles import UserRoleRepository
from auth_service.services.permission import PermissionService

PASSWORD_HASH = "$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG"


@pytest.fixture
async def service(pool_with_seed):
    return PermissionService(
        PermissionRepository(pool_with_seed),
        RoleRepository(pool_with_seed),
        UserRoleRepository(pool_with_seed),
    )


@pytest.fixture
async def make_user(database_url):
    async def create(name: str, *system_roles: UUID) -> UUID:
        conn = await asyncpg.connect(database_url)
        try:
            user_id = await conn.fetchval(
                """
                INSERT INTO users (username, email, hashed_password, password_change_required, is_active)
                VALUES ($1, $2, $3, false, true) RETURNING id
                """,
                name,
                f"{name}@example.com",
                PASSWORD_HASH,
            )
            for role_id in system_roles:
                await conn.execute(
                    """
                    INSERT INTO user_system_roles (user_id, role_id, granted_by, granted_at)
                    VALUES ($1, $2, $1, now())
                    """,
                    user_id,
                    role_id,
                )
            return user_id
        finally:
            await conn.close()

    return create


@pytest.fixture
async def make_project(database_url):
    async def create(owner_id: UUID) -> UUID:
        conn = await asyncpg.connect(database_url)
        try:
            return await conn.fetchval(
                "INSERT INTO projects (name, description, owner_id) VALUES ('P', 'd', $1) RETURNING id",
                owner_id,
            )
        finally:
            await conn.close()

    return create


class FailingAuditRepo:
    async def log(self, **kwargs):
        raise RuntimeError("audit store is down")


async def test_audit_write_failure_does_not_break_the_operation(pool_with_seed, make_user):
    admin = await make_user("auditadmin", SUPERADMIN_ROLE_ID)
    target = await make_user("audittarget")
    service = PermissionService(
        PermissionRepository(pool_with_seed),
        RoleRepository(pool_with_seed),
        UserRoleRepository(pool_with_seed),
        FailingAuditRepo(),  # type: ignore[arg-type]
    )

    assignment = await service.grant_system_role(admin, target, ADMIN_ROLE_ID)
    assert assignment.role_id == ADMIN_ROLE_ID


async def test_audit_entries_are_written_when_repo_is_configured(pool_with_seed, make_user):
    admin = await make_user("auditwriter", SUPERADMIN_ROLE_ID)
    target = await make_user("auditsubject")
    service = PermissionService(
        PermissionRepository(pool_with_seed),
        RoleRepository(pool_with_seed),
        UserRoleRepository(pool_with_seed),
        AuditRepository(pool_with_seed),
    )

    await service.grant_system_role(admin, target, ADMIN_ROLE_ID, ip_address="10.0.0.1")
    await service.revoke_system_role(admin, target, ADMIN_ROLE_ID, user_agent="pytest")

    rows = await pool_with_seed.fetch("SELECT action FROM audit_log")
    assert len(rows) == 2
    assert {r["action"] for r in rows} == {"role.grant", "role.revoke"}


async def test_has_permission_covers_superadmin_member_and_stranger(
    service, make_user, make_project
):
    superadmin = await make_user("hpsuper", SUPERADMIN_ROLE_ID)
    admin = await make_user("hpadmin", ADMIN_ROLE_ID)
    stranger = await make_user("hpstranger")
    project_id = await make_project(admin)

    assert await service.has_permission(superadmin, "anything.at.all") is True
    assert await service.has_permission(admin, "users.list") is True
    assert await service.has_permission(stranger, "users.list") is False
    assert await service.has_permission(admin, "experiments.view", project_id) is True
    assert await service.has_permission(stranger, "experiments.view", project_id) is False


async def test_set_admin_role_grants_and_revokes(service, make_user):
    actor = await make_user("saactor", ADMIN_ROLE_ID)
    target = await make_user("satarget")

    await service.set_admin_role(actor, target, grant=True)
    assert "admin" in [n.lower() for n in await service.list_system_role_names(target)]

    await service.set_admin_role(actor, target, grant=False)
    assert "admin" not in [n.lower() for n in await service.list_system_role_names(target)]


async def test_revoke_non_superadmin_system_role(service, make_user):
    actor = await make_user("rvactor", SUPERADMIN_ROLE_ID)
    target = await make_user("rvtarget", ADMIN_ROLE_ID)

    assert await service.revoke_system_role(actor, target, ADMIN_ROLE_ID) is True
    assert await service.revoke_system_role(actor, target, ADMIN_ROLE_ID) is False


async def test_revoke_superadmin_is_allowed_while_another_remains(service, make_user):
    first = await make_user("sa_first", SUPERADMIN_ROLE_ID)
    second = await make_user("sa_second", SUPERADMIN_ROLE_ID)

    assert await service.revoke_system_role(first, second, SUPERADMIN_ROLE_ID) is True


async def test_revoke_project_role_other_than_owner(service, make_user, make_project):
    owner = await make_user("prowner")
    member = await make_user("prmember")
    project_id = await make_project(owner)
    await service.grant_project_role(owner, project_id, member, PROJECT_EDITOR_ROLE_ID)

    revoked = await service.revoke_project_role(owner, project_id, member, PROJECT_EDITOR_ROLE_ID)
    assert revoked is True


async def test_cannot_revoke_the_only_project_owner(service, make_user, make_project):
    owner = await make_user("soloowner")
    project_id = await make_project(owner)

    with pytest.raises(ConflictError, match="last owner"):
        await service.revoke_project_role(owner, project_id, owner, PROJECT_OWNER_ROLE_ID)


async def test_a_second_owner_can_be_demoted(service, make_user, make_project):
    owner = await make_user("twoowner1")
    other = await make_user("twoowner2")
    project_id = await make_project(owner)
    await service.grant_project_role(owner, project_id, other, PROJECT_OWNER_ROLE_ID)

    assert await service.revoke_project_role(owner, project_id, other, PROJECT_OWNER_ROLE_ID)


async def test_validate_project_role_rejects_system_roles(service):
    await service.validate_project_role(PROJECT_VIEWER_ROLE_ID)
    with pytest.raises(ForbiddenError, match="system role as a project role"):
        await service.validate_project_role(ADMIN_ROLE_ID)


async def test_project_role_requires_project_id(service, make_user):
    actor = await make_user("noprojid", SUPERADMIN_ROLE_ID)
    with pytest.raises(ForbiddenError, match="project_id required"):
        await service.create_custom_role(
            actor, "Orphan", ScopeType.PROJECT, ["experiments.view"]
        )


async def test_custom_role_rejects_permissions_of_the_wrong_scope(service, make_user):
    actor = await make_user("wrongscope", SUPERADMIN_ROLE_ID)
    with pytest.raises(ForbiddenError, match="wrong scope"):
        await service.create_custom_role(
            actor, "Mixed", ScopeType.SYSTEM, ["users.list", "experiments.view"]
        )


async def test_custom_role_rejects_duplicate_names(service, make_user):
    actor = await make_user("dupnames", SUPERADMIN_ROLE_ID)
    await service.create_custom_role(actor, "Unique name", ScopeType.SYSTEM, ["users.list"])
    with pytest.raises(ConflictError, match="already exists"):
        await service.create_custom_role(actor, "Unique name", ScopeType.SYSTEM, ["users.list"])


async def test_project_custom_role_lifecycle(service, make_user, make_project):
    owner = await make_user("pcrowner")
    project_id = await make_project(owner)

    role = await service.create_custom_role(
        owner,
        "Reader",
        ScopeType.PROJECT,
        ["experiments.view"],
        project_id=project_id,
        description="read only",
    )
    assert role.project_id == project_id

    updated = await service.update_custom_role(
        owner, role.id, name="Reader 2", permissions=["experiments.view", "runs.create"]
    )
    assert updated.name == "Reader 2"

    await service.delete_custom_role(owner, role.id)
    with pytest.raises(NotFoundError):
        await service.update_custom_role(owner, role.id, name="gone")


async def test_update_custom_role_validates_permissions(service, make_user):
    actor = await make_user("updvalid", SUPERADMIN_ROLE_ID)
    role = await service.create_custom_role(actor, "Updatable", ScopeType.SYSTEM, ["users.list"])

    with pytest.raises(NotFoundError, match="Unknown permissions"):
        await service.update_custom_role(actor, role.id, permissions=["not.a.permission"])
    with pytest.raises(ForbiddenError, match="wrong scope"):
        await service.update_custom_role(actor, role.id, permissions=["experiments.view"])

    renamed = await service.update_custom_role(actor, role.id, name="Updated", permissions=[])
    assert renamed.name == "Updated"


async def test_builtin_roles_cannot_be_modified_or_deleted(service, make_user):
    actor = await make_user("builtin", SUPERADMIN_ROLE_ID)
    with pytest.raises(ForbiddenError, match="built-in"):
        await service.update_custom_role(actor, ADMIN_ROLE_ID, name="hacked")
    with pytest.raises(ForbiddenError, match="built-in"):
        await service.delete_custom_role(actor, ADMIN_ROLE_ID)


async def test_unknown_role_is_not_found(service, make_user):
    actor = await make_user("ghostrole", SUPERADMIN_ROLE_ID)
    with pytest.raises(NotFoundError):
        await service.delete_custom_role(actor, uuid4())
