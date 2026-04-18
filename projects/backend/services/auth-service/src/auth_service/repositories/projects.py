"""Project repository."""
from __future__ import annotations

from uuid import UUID

from auth_service.core.exceptions import NotFoundError
from auth_service.domain.models import Project, UserProjectRole
from auth_service.repositories.base import BaseRepository


class ProjectRepository(BaseRepository):
    """Repository for project operations."""

    async def is_member(self, project_id: UUID, user_id: UUID) -> bool:
        """Check if user is a member of the project."""
        row = await self._fetchrow(
            "SELECT EXISTS("
            "  SELECT 1 FROM user_project_roles "
            "  WHERE project_id = $1 AND user_id = $2 "
            "  AND (expires_at IS NULL OR expires_at > now())"
            ") AS is_member",
            project_id, user_id,
        )
        return bool(row["is_member"]) if row else False

    async def get_member_roles(self, project_id: UUID, user_id: UUID) -> list[UserProjectRole]:
        """Get all roles for a user in a project."""
        rows = await self._fetch(
            "SELECT user_id, project_id, role_id, granted_by, granted_at, expires_at "
            "FROM user_project_roles "
            "WHERE project_id = $1 AND user_id = $2 "
            "AND (expires_at IS NULL OR expires_at > now())",
            project_id, user_id,
        )
        return [UserProjectRole.from_row(dict(r)) for r in rows]

    async def get_member_role_names(self, project_id: UUID, user_id: UUID) -> list[str]:
        """Get role names for a user in a project."""
        rows = await self._fetch(
            "SELECT r.name FROM user_project_roles upr "
            "JOIN roles r ON r.id = upr.role_id "
            "WHERE upr.project_id = $1 AND upr.user_id = $2 "
            "AND (upr.expires_at IS NULL OR upr.expires_at > now())",
            project_id, user_id,
        )
        return [row["name"] for row in rows]

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

    async def list_by_user(
        self,
        user_id: UUID,
        search: str | None = None,
        role: str | None = None,
        limit: int = 20,
        offset: int = 0,
    ) -> tuple[list[Project], int]:
        """List projects where user has any active role, with optional filtering.

        Returns a tuple of (projects, total_count).
        """
        base_where = """
            FROM projects p
            INNER JOIN user_project_roles upr ON p.id = upr.project_id
            INNER JOIN roles r ON r.id = upr.role_id
            WHERE upr.user_id = $1
              AND (upr.expires_at IS NULL OR upr.expires_at > now())
              AND ($2::text IS NULL OR p.name ILIKE '%' || $2 || '%')
              AND ($3::text IS NULL OR r.name = $3)
        """
        count_query = f"SELECT COUNT(DISTINCT p.id) {base_where}"
        count_row = await self._fetchrow(count_query, user_id, search, role)
        total = int(count_row["count"]) if count_row else 0

        select_query = f"""
            SELECT DISTINCT ON (p.created_at, p.id)
                   p.id, p.name, p.description, p.owner_id, p.created_at, p.updated_at
            {base_where}
            ORDER BY p.created_at DESC, p.id
            LIMIT $4 OFFSET $5
        """
        rows = await self._fetch(select_query, user_id, search, role, limit, offset)
        return [Project.from_row(dict(row)) for row in rows], total

    async def list_all(self) -> list[Project]:
        """List all projects (for superadmin/admin views)."""
        query = """
            SELECT id, name, description, owner_id, created_at, updated_at
            FROM projects
            ORDER BY created_at DESC
        """
        rows = await self._fetch(query)
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
        updates: list[str] = []
        params: list[str | UUID | None] = []
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
