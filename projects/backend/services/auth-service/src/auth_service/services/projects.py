"""Project service."""
from __future__ import annotations

import structlog
from uuid import UUID

from auth_service.core.exceptions import ForbiddenError, NotFoundError
from auth_service.domain.models import (
    PROJECT_OWNER_ROLE_ID,
    AuditAction,
    Project,
    ScopeType,
    UserProjectRole,
)
from auth_service.repositories.audit import AuditRepository
from auth_service.repositories.projects import ProjectRepository
from auth_service.repositories.users import UserRepository
from auth_service.repositories.user_roles import UserRoleRepository
from auth_service.services.permission import PermissionService

logger = structlog.get_logger(__name__)


class ProjectService:
    """Service for project operations."""

    def __init__(
        self,
        project_repo: ProjectRepository,
        user_repo: UserRepository,
        user_role_repo: UserRoleRepository,
        permission_service: PermissionService,
        audit_repo: AuditRepository | None = None,
    ) -> None:
        self.project_repo = project_repo
        self.user_repo = user_repo
        self.user_role_repo = user_role_repo
        self.perm_svc = permission_service
        self._audit_repo = audit_repo

    async def _audit(
        self,
        actor_id: UUID,
        action: str,
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
            scope_type = ScopeType.PROJECT if scope_id else ScopeType.SYSTEM
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

    async def create_project(
        self,
        name: str,
        description: str | None,
        owner_id: UUID,
        ip_address: str | None = None,
        user_agent: str | None = None,
    ) -> Project:
        """Create a new project. Any authenticated user can create a project."""
        user = await self.user_repo.get_by_id(owner_id)
        if not user:
            raise NotFoundError(f"User {owner_id} not found")

        project = await self.project_repo.create(name, description, owner_id)
        await self._audit(
            owner_id, AuditAction.PROJECT_CREATE,
            scope_id=project.id,
            target_type="project", target_id=str(project.id),
            details={"name": name},
            ip_address=ip_address, user_agent=user_agent,
        )
        return project

    async def get_project(self, project_id: UUID, user_id: UUID) -> Project:
        """Get project by ID (user must have 'project.members.view' permission)."""
        await self.perm_svc.ensure_permission(user_id, "project.members.view", project_id)
        
        project = await self.project_repo.get_by_id(project_id)
        if not project:
            raise NotFoundError(f"Project {project_id} not found")

        return project

    async def list_user_projects(
        self,
        user_id: UUID,
        search: str | None = None,
        role: str | None = None,
        limit: int = 20,
        offset: int = 0,
    ) -> tuple[list[Project], int]:
        """List projects where user is a member, with optional filtering.

        Returns (projects, total_count).
        """
        return await self.project_repo.list_by_user(
            user_id, search=search, role=role, limit=limit, offset=offset,
        )

    async def list_all_projects(self, user_id: UUID) -> list[Project]:
        """List all projects. Requires 'projects.list' or superadmin."""
        # Check if user has system-level permission to list all projects
        # For now, allow if superadmin or has projects.list (if such permission exists)
        # Otherwise fall back to list_user_projects
        if await self.user_role_repo.is_superadmin(user_id):
            return await self.project_repo.list_all()
        projects, _ = await self.project_repo.list_by_user(user_id)
        return projects

    async def update_project(
        self,
        project_id: UUID,
        user_id: UUID,
        name: str | None = None,
        description: str | None = None,
    ) -> Project:
        """Update project. Requires 'project.settings.update' permission."""
        await self.perm_svc.ensure_permission(user_id, "project.settings.update", project_id)
        
        project = await self.project_repo.get_by_id(project_id)
        if not project:
            raise NotFoundError(f"Project {project_id} not found")

        return await self.project_repo.update(project_id, name, description)

    async def delete_project(
        self,
        project_id: UUID,
        user_id: UUID,
        ip_address: str | None = None,
        user_agent: str | None = None,
    ) -> None:
        """Delete project. Requires 'project.settings.delete' permission."""
        await self.perm_svc.ensure_permission(user_id, "project.settings.delete", project_id)

        project = await self.project_repo.get_by_id(project_id)
        if not project:
            raise NotFoundError(f"Project {project_id} not found")

        await self.project_repo.delete(project_id)
        await self._audit(
            user_id, AuditAction.PROJECT_DELETE,
            scope_id=project_id,
            target_type="project", target_id=str(project_id),
            details={"name": project.name},
            ip_address=ip_address, user_agent=user_agent,
        )

    async def add_member(
        self,
        project_id: UUID,
        requester_id: UUID,
        new_user_id: UUID,
        role_id: UUID,
        ip_address: str | None = None,
        user_agent: str | None = None,
    ) -> UserProjectRole:
        """Add member to project. Requires 'project.members.invite' permission."""
        await self.perm_svc.ensure_permission(requester_id, "project.members.invite", project_id)

        new_user = await self.user_repo.get_by_id(new_user_id)
        if not new_user:
            raise NotFoundError(f"User {new_user_id} not found")

        await self.perm_svc.validate_project_role(role_id)

        assignment = await self.user_role_repo.grant_project_role(
            new_user_id, project_id, role_id, requester_id,
        )
        await self._audit(
            requester_id, AuditAction.PROJECT_MEMBER_ADD,
            scope_id=project_id,
            target_type="user", target_id=str(new_user_id),
            details={"role_id": str(role_id)},
            ip_address=ip_address, user_agent=user_agent,
        )
        return assignment

    async def remove_member(
        self,
        project_id: UUID,
        requester_id: UUID,
        member_user_id: UUID,
        role_id: UUID | None = None,
        ip_address: str | None = None,
        user_agent: str | None = None,
    ) -> bool:
        """Remove member from project. Requires 'project.members.remove' permission.

        With ``role_id`` only that role is revoked; without it the user loses
        every role in the project, i.e. leaves the project.
        """
        await self.perm_svc.ensure_permission(requester_id, "project.members.remove", project_id)

        project = await self.project_repo.get_by_id(project_id)
        if not project:
            raise NotFoundError(f"Project {project_id} not found")

        if member_user_id == project.owner_id:
            raise ForbiddenError("Cannot remove project owner")

        if role_id is None:
            removed = await self.user_role_repo.revoke_all_project_roles(
                member_user_id, project_id,
            )
            if removed == 0:
                raise NotFoundError(f"User {member_user_id} is not a member of the project")
            result = True
        else:
            result = await self.user_role_repo.revoke_project_role(
                member_user_id, project_id, role_id,
            )
        await self._audit(
            requester_id, AuditAction.PROJECT_MEMBER_REMOVE,
            scope_id=project_id,
            target_type="user", target_id=str(member_user_id),
            details={"role_id": str(role_id) if role_id else "all"},
            ip_address=ip_address, user_agent=user_agent,
        )
        return result

    async def update_member_role(
        self,
        project_id: UUID,
        requester_id: UUID,
        member_user_id: UUID,
        old_role_id: UUID | None,
        new_role_id: UUID,
    ) -> UserProjectRole:
        """Update member role. Requires 'project.members.change_role' permission.

        With ``old_role_id`` only that role is swapped for the new one; without
        it all of the member's roles are replaced by the new role in one
        transaction. The project owner's role cannot be changed.
        """
        await self.perm_svc.ensure_permission(
            requester_id, "project.members.change_role", project_id,
        )

        project = await self.project_repo.get_by_id(project_id)
        if not project:
            raise NotFoundError(f"Project {project_id} not found")

        if member_user_id == project.owner_id:
            raise ForbiddenError("Cannot change the project owner's role")

        if old_role_id is not None:
            # grant_project_role validates scope_type and writes the audit entry
            new_assignment = await self.perm_svc.grant_project_role(
                requester_id, project_id, member_user_id, new_role_id,
            )
            # Revoke old role after grant succeeds; same role means nothing to revoke
            if old_role_id != new_role_id:
                await self.user_role_repo.revoke_project_role(
                    member_user_id, project_id, old_role_id,
                )
            return new_assignment

        await self.perm_svc.validate_project_role(new_role_id)

        result = await self.user_role_repo.replace_project_roles(
            member_user_id, project_id, new_role_id, requester_id,
        )
        # No active role (none at all, or only expired ones) means not a member
        if result is None:
            raise NotFoundError(f"User {member_user_id} is not a member of the project")
        assignment, old_role_ids = result
        if old_role_ids != [new_role_id]:
            await self._audit(
                requester_id, AuditAction.PROJECT_MEMBER_CHANGE_ROLE,
                scope_id=project_id,
                target_type="user", target_id=str(member_user_id),
                details={
                    "old_role_ids": [str(r) for r in old_role_ids],
                    "role_id": str(new_role_id),
                },
            )
        return assignment

    async def list_members(self, project_id: UUID, user_id: UUID) -> list[dict]:
        """List project members. Requires 'project.members.view' permission."""
        await self.perm_svc.ensure_permission(user_id, "project.members.view", project_id)
        
        return await self.user_role_repo.list_project_members(project_id)

