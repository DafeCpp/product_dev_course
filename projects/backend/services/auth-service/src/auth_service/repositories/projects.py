"""Project repository."""
from __future__ import annotations

from uuid import UUID

import asyncpg  # type: ignore[import-untyped]

from auth_service.core.exceptions import NotFoundError
from auth_service.domain.models import Project, ProjectMember
from auth_service.repositories.base import BaseRepository


class ProjectRepository(BaseRepository):
    """Repository for project operations."""

    async def create(
        self,
        name: str,
        description: str | None,
        owner_id: UUID,
    ) -> Project:
        """Create a new project."""
        query = """
            INSERT INTO projects (name, description, owner_id)
            VALUES ($1, $2, $3)
            RETURNING id, name, description, owner_id, created_at, updated_at
        """
        row = await self._fetchrow(query, name, description, owner_id)
        if not row:
            raise RuntimeError("Failed to create project")
        return Project.from_row(dict(row))

    async def get_by_id(self, project_id: UUID) -> Project | None:
        """Get project by ID."""
        query = """
            SELECT id, name, description, owner_id, created_at, updated_at
            FROM projects
            WHERE id = $1
        """
        row = await self._fetchrow(query, project_id)
        if not row:
            return None
        return Project.from_row(dict(row))

    async def get_by_id_or_raise(self, project_id: UUID) -> Project:
        """Get project by ID or raise NotFoundError."""
        project = await self.get_by_id(project_id)
        if not project:
            raise NotFoundError(f"Project {project_id} not found")
        return project

    async def list_by_user(self, user_id: UUID) -> list[Project]:
        """List all projects where user is a member."""
        query = """
            SELECT DISTINCT p.id, p.name, p.description, p.owner_id, p.created_at, p.updated_at
            FROM projects p
            INNER JOIN project_members pm ON p.id = pm.project_id
            WHERE pm.user_id = $1
            ORDER BY p.created_at DESC
        """
        rows = await self._fetch(query, user_id)
        return [Project.from_row(dict(row)) for row in rows]

    async def list_by_owner(self, owner_id: UUID) -> list[Project]:
        """List all projects owned by user."""
        query = """
            SELECT id, name, description, owner_id, created_at, updated_at
            FROM projects
            WHERE owner_id = $1
            ORDER BY created_at DESC
        """
        rows = await self._fetch(query, owner_id)
        return [Project.from_row(dict(row)) for row in rows]

    async def update(
        self,
        project_id: UUID,
        name: str | None = None,
        description: str | None = None,
    ) -> Project:
        """Update project."""
        updates = []
        params = []
        param_idx = 1

        if name is not None:
            updates.append(f"name = ${param_idx}")
            params.append(name)
            param_idx += 1

        if description is not None:
            updates.append(f"description = ${param_idx}")
            params.append(description)
            param_idx += 1

        if not updates:
            return await self.get_by_id_or_raise(project_id)

        params.append(project_id)
        query = f"""
            UPDATE projects
            SET {', '.join(updates)}, updated_at = now()
            WHERE id = ${param_idx}
            RETURNING id, name, description, owner_id, created_at, updated_at
        """
        row = await self._fetchrow(query, *params)
        if not row:
            raise NotFoundError(f"Project {project_id} not found")
        return Project.from_row(dict(row))

    async def delete(self, project_id: UUID) -> None:
        """Delete project."""
        query = "DELETE FROM projects WHERE id = $1"
        result = await self._execute(query, project_id)
        if result == "DELETE 0":
            raise NotFoundError(f"Project {project_id} not found")

    async def is_member(self, project_id: UUID, user_id: UUID) -> bool:
        """Check if user is a member of project."""
        query = """
            SELECT EXISTS(
                SELECT 1 FROM project_members
                WHERE project_id = $1 AND user_id = $2
            )
        """
        row = await self._fetchrow(query, project_id, user_id)
        return bool(row["exists"]) if row else False

    async def get_member_role(self, project_id: UUID, user_id: UUID) -> str | None:
        """Get user's role in project."""
        query = """
            SELECT role FROM project_members
            WHERE project_id = $1 AND user_id = $2
        """
        row = await self._fetchrow(query, project_id, user_id)
        return row["role"] if row else None

    async def add_member(
        self,
        project_id: UUID,
        user_id: UUID,
        role: str,
    ) -> ProjectMember:
        """Add member to project."""
        query = """
            INSERT INTO project_members (project_id, user_id, role)
            VALUES ($1, $2, $3)
            ON CONFLICT (project_id, user_id) DO UPDATE
            SET role = EXCLUDED.role
            RETURNING project_id, user_id, role, created_at
        """
        row = await self._fetchrow(query, project_id, user_id, role)
        if not row:
            raise RuntimeError("Failed to add project member")
        return ProjectMember.from_row(dict(row))

    async def remove_member(self, project_id: UUID, user_id: UUID) -> None:
        """Remove member from project."""
        query = "DELETE FROM project_members WHERE project_id = $1 AND user_id = $2"
        result = await self._execute(query, project_id, user_id)
        if result == "DELETE 0":
            raise NotFoundError(f"Member {user_id} not found in project {project_id}")

    async def list_members(self, project_id: UUID) -> list[dict]:
        """List all members of project with usernames."""
        query = """
            SELECT pm.project_id, pm.user_id, pm.role, pm.created_at, u.username
            FROM project_members pm
            INNER JOIN users u ON pm.user_id = u.id
            WHERE pm.project_id = $1
            ORDER BY pm.created_at ASC
        """
        rows = await self._fetch(query, project_id)
        return [dict(row) for row in rows]

