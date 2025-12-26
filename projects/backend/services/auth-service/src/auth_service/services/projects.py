"""Project service."""
from __future__ import annotations

from uuid import UUID

from auth_service.core.exceptions import ForbiddenError, NotFoundError
from auth_service.domain.models import Project, ProjectMember
from auth_service.repositories.projects import ProjectRepository
from auth_service.repositories.users import UserRepository


class ProjectService:
    """Service for project operations."""

    def __init__(
        self,
        project_repo: ProjectRepository,
        user_repo: UserRepository,
    ) -> None:
        self.project_repo = project_repo
        self.user_repo = user_repo

    async def create_project(
        self,
        name: str,
        description: str | None,
        owner_id: UUID,
    ) -> Project:
        """Create a new project."""
        # Check if user exists
        user = await self.user_repo.get_by_id(owner_id)
        if not user:
            raise NotFoundError(f"User {owner_id} not found")

        # Create project (trigger will automatically add owner as member)
        return await self.project_repo.create(name, description, owner_id)

    async def get_project(self, project_id: UUID, user_id: UUID) -> Project:
        """Get project by ID (user must be a member)."""
        project = await self.project_repo.get_by_id(project_id)
        if not project:
            raise NotFoundError(f"Project {project_id} not found")

        # Check if user is a member
        is_member = await self.project_repo.is_member(project_id, user_id)
        if not is_member:
            raise ForbiddenError("You are not a member of this project")

        return project

    async def list_user_projects(self, user_id: UUID) -> list[Project]:
        """List all projects where user is a member."""
        return await self.project_repo.list_by_user(user_id)

    async def update_project(
        self,
        project_id: UUID,
        user_id: UUID,
        name: str | None = None,
        description: str | None = None,
    ) -> Project:
        """Update project (only owner or editor can update)."""
        project = await self.project_repo.get_by_id(project_id)
        if not project:
            raise NotFoundError(f"Project {project_id} not found")

        # Check permissions
        role = await self.project_repo.get_member_role(project_id, user_id)
        if not role or role not in ("owner", "editor"):
            raise ForbiddenError("Only owner or editor can update project")

        return await self.project_repo.update(project_id, name, description)

    async def delete_project(self, project_id: UUID, user_id: UUID) -> None:
        """Delete project (only owner can delete)."""
        project = await self.project_repo.get_by_id(project_id)
        if not project:
            raise NotFoundError(f"Project {project_id} not found")

        # Only owner can delete
        if project.owner_id != user_id:
            raise ForbiddenError("Only project owner can delete project")

        await self.project_repo.delete(project_id)

    async def add_member(
        self,
        project_id: UUID,
        user_id: UUID,  # User adding the member
        new_user_id: UUID,  # User being added
        role: str,
    ) -> ProjectMember:
        """Add member to project (only owner or editor can add members)."""
        project = await self.project_repo.get_by_id(project_id)
        if not project:
            raise NotFoundError(f"Project {project_id} not found")

        # Check if new user exists
        new_user = await self.user_repo.get_by_id(new_user_id)
        if not new_user:
            raise NotFoundError(f"User {new_user_id} not found")

        # Check permissions
        user_role = await self.project_repo.get_member_role(project_id, user_id)
        if not user_role or user_role not in ("owner", "editor"):
            raise ForbiddenError("Only owner or editor can add members")

        return await self.project_repo.add_member(project_id, new_user_id, role)

    async def remove_member(
        self,
        project_id: UUID,
        user_id: UUID,  # User removing the member
        member_user_id: UUID,  # User being removed
    ) -> None:
        """Remove member from project (only owner can remove members)."""
        project = await self.project_repo.get_by_id(project_id)
        if not project:
            raise NotFoundError(f"Project {project_id} not found")

        # Check permissions - only owner can remove members
        user_role = await self.project_repo.get_member_role(project_id, user_id)
        if not user_role or user_role != "owner":
            raise ForbiddenError("Only owner can remove members")

        # Cannot remove owner
        if member_user_id == project.owner_id:
            raise ForbiddenError("Cannot remove project owner")

        await self.project_repo.remove_member(project_id, member_user_id)

    async def update_member_role(
        self,
        project_id: UUID,
        user_id: UUID,  # User updating the role
        member_user_id: UUID,  # User whose role is being updated
        new_role: str,
    ) -> ProjectMember:
        """Update member role (only owner can update roles)."""
        project = await self.project_repo.get_by_id(project_id)
        if not project:
            raise NotFoundError(f"Project {project_id} not found")

        # Check permissions - only owner can update roles
        user_role = await self.project_repo.get_member_role(project_id, user_id)
        if not user_role or user_role != "owner":
            raise ForbiddenError("Only owner can update member roles")

        # Cannot change owner's role
        if member_user_id == project.owner_id:
            raise ForbiddenError("Cannot change project owner's role")

        return await self.project_repo.add_member(project_id, member_user_id, new_role)

    async def list_members(self, project_id: UUID, user_id: UUID) -> list[dict]:
        """List project members (user must be a member)."""
        project = await self.project_repo.get_by_id(project_id)
        if not project:
            raise NotFoundError(f"Project {project_id} not found")

        # Check if user is a member
        is_member = await self.project_repo.is_member(project_id, user_id)
        if not is_member:
            raise ForbiddenError("You are not a member of this project")

        return await self.project_repo.list_members(project_id)

