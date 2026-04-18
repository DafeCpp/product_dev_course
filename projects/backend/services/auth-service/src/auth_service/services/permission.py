"""Permission service — RBAC v2 business logic."""
from __future__ import annotations

from uuid import UUID

from datetime import datetime

import structlog
from asyncpg.exceptions import UniqueViolationError  # type: ignore[import-untyped]

from auth_service.core.exceptions import ConflictError, ForbiddenError, NotFoundError
from auth_service.domain.dto import EffectivePermissionsResponse
from auth_service.domain.models import (
    ADMIN_ROLE_ID,
    PROJECT_OWNER_ROLE_ID,
    SUPERADMIN_ROLE_ID,
    AuditAction,
    Role,
    ScopeType,
    UserProjectRole,
    UserSystemRole,
)
from auth_service.repositories.audit import AuditRepository
from auth_service.repositories.permissions import PermissionRepository
from auth_service.repositories.roles import RoleRepository
from auth_service.repositories.user_roles import UserRoleRepository

logger = structlog.get_logger(__name__)


class PermissionService:
    """Service for RBAC v2 permission checks, role management, and role assignments."""

    def __init__(
        self,
        permission_repo: PermissionRepository,
        role_repo: RoleRepository,
        user_role_repo: UserRoleRepository,
        audit_repo: AuditRepository | None = None,
    ) -> None:
        self._perm_repo = permission_repo
        self._role_repo = role_repo
        self._user_role_repo = user_role_repo
        self._audit_repo = audit_repo

    async def _audit(
        self,
        actor_id: UUID,
        action: str,
        scope_type: str = ScopeType.SYSTEM,
        *,
        scope_id: UUID | None = None,
        target_type: str | None = None,
        target_id: str | None = None,
        details: dict | None = None,
        ip_address: str | None = None,
        user_agent: str | None = None,
    ) -> None:
        if self._audit_repo is None:
            return
        try:
            await self._audit_repo.log(
                actor_id=actor_id,
                action=action,
                scope_type=scope_type,
                scope_id=scope_id,
                target_type=target_type,
                target_id=target_id,
                details=details,
                ip_address=ip_address,
                user_agent=user_agent,
            )
        except Exception as e:
            logger.warning("Audit log write failed", action=action, error=str(e))

    # ── Permission checks ──────────────────────────────────────────────

    async def get_effective_permissions(
        self, user_id: UUID, project_id: UUID | None = None,
    ) -> EffectivePermissionsResponse:
        """Return effective permissions for a user, respecting expires_at."""
        is_sa = await self._user_role_repo.is_superadmin(user_id)

        system_perms: list[str] = []
        project_perms: list[str] = []

        if not is_sa:
            all_perms = await self._user_role_repo.get_effective_permissions(
                user_id, project_id,
            )
            # Split into system / project
            all_perm_objects = await self._perm_repo.list_all()
            perm_scope_map = {p.id: p.scope_type for p in all_perm_objects}
            for pid in all_perms:
                scope = perm_scope_map.get(pid)
                if scope == ScopeType.SYSTEM:
                    system_perms.append(pid)
                elif scope == ScopeType.PROJECT:
                    project_perms.append(pid)

        return EffectivePermissionsResponse(
            user_id=str(user_id),
            is_superadmin=is_sa,
            system_permissions=system_perms,
            project_permissions=project_perms,
        )

    async def ensure_permission(
        self,
        user_id: UUID,
        permission_id: str,
        project_id: UUID | None = None,
    ) -> None:
        """Raise ForbiddenError if user does not have the required permission."""
        if await self._user_role_repo.is_superadmin(user_id):
            return  # superadmin has all permissions implicitly

        perms = await self._user_role_repo.get_effective_permissions(user_id, project_id)
        if permission_id not in perms:
            raise ForbiddenError(f"Missing permission: {permission_id}")

    async def has_permission(
        self,
        user_id: UUID,
        permission_id: str,
        project_id: UUID | None = None,
    ) -> bool:
        """Check if user has a specific permission (without raising)."""
        if await self._user_role_repo.is_superadmin(user_id):
            return True
        perms = await self._user_role_repo.get_effective_permissions(user_id, project_id)
        return permission_id in perms

    # ── Public helpers (used by AuthService) ──────────────────────────

    async def is_superadmin(self, user_id: UUID) -> bool:
        """Check if user has active superadmin role."""
        return await self._user_role_repo.is_superadmin(user_id)

    async def count_superadmins(self) -> int:
        """Count users with active superadmin role."""
        return await self._user_role_repo.count_superadmins()

    async def list_system_role_names(self, user_id: UUID) -> list[str]:
        """List active system role names for a user."""
        return await self._user_role_repo.list_system_role_names(user_id)

    async def bootstrap_grant_superadmin(self, user_id: UUID) -> None:
        """Grant superadmin role during bootstrap (no grantor permission check)."""
        await self._user_role_repo.grant_system_role(user_id, SUPERADMIN_ROLE_ID, user_id)

    async def set_admin_role(
        self,
        actor_id: UUID,
        target_user_id: UUID,
        *,
        grant: bool,
    ) -> None:
        """Grant or revoke the built-in 'admin' role. No extra permission check
        (caller must already hold 'users.update')."""
        if grant:
            await self._user_role_repo.grant_system_role(
                target_user_id, ADMIN_ROLE_ID, actor_id,
            )
        else:
            await self._user_role_repo.revoke_system_role(target_user_id, ADMIN_ROLE_ID)

    async def batch_list_system_role_names(
        self, user_ids: list[UUID],
    ) -> dict[UUID, list[str]]:
        """List system role names for multiple users in one query."""
        return await self._user_role_repo.batch_list_system_role_names(user_ids)

    async def get_role_permissions(self, role_id: UUID) -> list[str]:
        """Get permission IDs for a role (for API response building)."""
        return await self._role_repo.get_permissions(role_id)

    async def get_role_by_id_or_raise(self, role_id: UUID) -> "Role":
        """Get role by ID or raise NotFoundError."""
        return await self._role_repo.get_by_id_or_raise(role_id)

    # ── System role assignments ────────────────────────────────────────

    async def grant_system_role(
        self,
        grantor_id: UUID,
        target_user_id: UUID,
        role_id: UUID,
        expires_at: datetime | None = None,
        ip_address: str | None = None,
        user_agent: str | None = None,
    ) -> UserSystemRole:
        """Grant a system role. Grantor must have 'roles.assign'."""
        await self.ensure_permission(grantor_id, "roles.assign")

        role = await self._role_repo.get_by_id_or_raise(role_id)
        if role.scope_type != ScopeType.SYSTEM:
            raise ForbiddenError("Cannot assign a project role as a system role")

        assignment = await self._user_role_repo.grant_system_role(
            target_user_id, role_id, grantor_id, expires_at,
        )
        await self._audit(
            grantor_id, AuditAction.ROLE_GRANT,
            target_type="user", target_id=str(target_user_id),
            details={"role_id": str(role_id), "role_name": role.name, "scope": "system"},
            ip_address=ip_address, user_agent=user_agent,
        )
        return assignment

    async def revoke_system_role(
        self,
        grantor_id: UUID,
        target_user_id: UUID,
        role_id: UUID,
        ip_address: str | None = None,
        user_agent: str | None = None,
    ) -> bool:
        """Revoke a system role. Protects last superadmin."""
        await self.ensure_permission(grantor_id, "roles.assign")

        role = await self._role_repo.get_by_id_or_raise(role_id)
        if role.is_superadmin:
            count = await self._user_role_repo.count_superadmins()
            if count <= 1:
                raise ConflictError("Cannot revoke the last superadmin role")

        result = await self._user_role_repo.revoke_system_role(target_user_id, role_id)
        await self._audit(
            grantor_id, AuditAction.ROLE_REVOKE,
            target_type="user", target_id=str(target_user_id),
            details={"role_id": str(role_id), "role_name": role.name, "scope": "system"},
            ip_address=ip_address, user_agent=user_agent,
        )
        return result

    # ── Project role assignments ───────────────────────────────────────

    async def grant_project_role(
        self,
        grantor_id: UUID,
        project_id: UUID,
        target_user_id: UUID,
        role_id: UUID,
        ip_address: str | None = None,
        user_agent: str | None = None,
    ) -> UserProjectRole:
        """Grant a project role. Grantor must have 'project.members.change_role' in the project."""
        await self.ensure_permission(grantor_id, "project.members.change_role", project_id)

        role = await self._role_repo.get_by_id_or_raise(role_id)
        if role.scope_type != ScopeType.PROJECT:
            raise ForbiddenError("Cannot assign a system role as a project role")

        assignment = await self._user_role_repo.grant_project_role(
            target_user_id, project_id, role_id, grantor_id,
        )
        await self._audit(
            grantor_id, AuditAction.ROLE_GRANT,
            scope_type=ScopeType.PROJECT, scope_id=project_id,
            target_type="user", target_id=str(target_user_id),
            details={"role_id": str(role_id), "role_name": role.name, "scope": "project"},
            ip_address=ip_address, user_agent=user_agent,
        )
        return assignment

    async def revoke_project_role(
        self,
        grantor_id: UUID,
        project_id: UUID,
        target_user_id: UUID,
        role_id: UUID,
        ip_address: str | None = None,
        user_agent: str | None = None,
    ) -> bool:
        """Revoke a project role. Cannot remove the sole owner."""
        await self.ensure_permission(grantor_id, "project.members.change_role", project_id)

        if role_id == PROJECT_OWNER_ROLE_ID:
            owner_count = await self._user_role_repo.count_project_role(
                project_id, PROJECT_OWNER_ROLE_ID,
            )
            if owner_count <= 1:
                raise ConflictError("Cannot revoke the last owner of a project")

        result = await self._user_role_repo.revoke_project_role(
            target_user_id, project_id, role_id,
        )
        await self._audit(
            grantor_id, AuditAction.ROLE_REVOKE,
            scope_type=ScopeType.PROJECT, scope_id=project_id,
            target_type="user", target_id=str(target_user_id),
            details={"role_id": str(role_id), "scope": "project"},
            ip_address=ip_address, user_agent=user_agent,
        )
        return result

    async def validate_project_role(self, role_id: UUID) -> None:
        """Validate that a role is project-scoped. Raises ForbiddenError otherwise."""
        role = await self._role_repo.get_by_id_or_raise(role_id)
        if role.scope_type != ScopeType.PROJECT:
            raise ForbiddenError("Cannot assign a system role as a project role")

    # ── Custom role management ─────────────────────────────────────────

    async def create_custom_role(
        self,
        creator_id: UUID,
        name: str,
        scope_type: ScopeType,
        permissions: list[str],
        *,
        project_id: UUID | None = None,
        description: str | None = None,
    ) -> Role:
        """Create a custom role with permissions."""
        if scope_type == ScopeType.SYSTEM:
            await self.ensure_permission(creator_id, "roles.manage")
        else:
            if project_id is None:
                raise ForbiddenError("project_id required for project-scope roles")
            await self.ensure_permission(creator_id, "project.roles.manage", project_id)

        # Validate permissions exist and match scope
        valid_perms = await self._perm_repo.get_by_ids(permissions)
        valid_ids = {p.id for p in valid_perms}
        invalid = set(permissions) - valid_ids
        if invalid:
            raise NotFoundError(f"Unknown permissions: {', '.join(sorted(invalid))}")
        wrong_scope = {p.id for p in valid_perms if p.scope_type != scope_type}
        if wrong_scope:
            raise ForbiddenError(
                f"Permissions have wrong scope for this role: {', '.join(sorted(wrong_scope))}"
            )

        try:
            role = await self._role_repo.create(
                name, scope_type,
                project_id=project_id,
                description=description,
                created_by=creator_id,
            )
        except UniqueViolationError:
            raise ConflictError(f"Role with name '{name}' already exists")
        await self._role_repo.set_permissions(role.id, permissions)
        await self._audit(
            creator_id, AuditAction.ROLE_CREATE,
            scope_type=scope_type.value if project_id is None else ScopeType.PROJECT,
            scope_id=project_id,
            target_type="role", target_id=str(role.id),
            details={"name": name, "permissions": permissions},
        )
        return role

    async def update_custom_role(
        self,
        updater_id: UUID,
        role_id: UUID,
        *,
        name: str | None = None,
        description: str | None = None,
        permissions: list[str] | None = None,
    ) -> Role:
        """Update a custom role. Cannot update built-in roles."""
        role = await self._role_repo.get_by_id_or_raise(role_id)
        if role.is_builtin:
            raise ForbiddenError("Cannot modify built-in roles")

        if role.scope_type == ScopeType.SYSTEM:
            await self.ensure_permission(updater_id, "roles.manage")
        else:
            await self.ensure_permission(updater_id, "project.roles.manage", role.project_id)

        if permissions is not None:
            valid_perms = await self._perm_repo.get_by_ids(permissions)
            valid_ids = {p.id for p in valid_perms}
            invalid = set(permissions) - valid_ids
            if invalid:
                raise NotFoundError(f"Unknown permissions: {', '.join(sorted(invalid))}")
            wrong_scope = {p.id for p in valid_perms if p.scope_type != role.scope_type}
            if wrong_scope:
                raise ForbiddenError(
                    f"Permissions have wrong scope for this role: {', '.join(sorted(wrong_scope))}"
                )
            await self._role_repo.set_permissions(role_id, permissions)

        updated = await self._role_repo.update(role_id, name=name, description=description)
        await self._audit(
            updater_id, AuditAction.ROLE_UPDATE,
            scope_type=role.scope_type.value,
            scope_id=role.project_id,
            target_type="role", target_id=str(role_id),
            details={"name": name, "permissions": permissions},
        )
        return updated

    async def delete_custom_role(self, deleter_id: UUID, role_id: UUID) -> None:
        """Delete a custom role. Cannot delete built-in roles."""
        role = await self._role_repo.get_by_id_or_raise(role_id)
        if role.is_builtin:
            raise ForbiddenError("Cannot delete built-in roles")

        if role.scope_type == ScopeType.SYSTEM:
            await self.ensure_permission(deleter_id, "roles.manage")
        else:
            await self.ensure_permission(deleter_id, "project.roles.manage", role.project_id)

        await self._role_repo.delete(role_id)
        await self._audit(
            deleter_id, AuditAction.ROLE_DELETE,
            scope_type=role.scope_type.value,
            scope_id=role.project_id,
            target_type="role", target_id=str(role_id),
            details={"name": role.name},
        )
