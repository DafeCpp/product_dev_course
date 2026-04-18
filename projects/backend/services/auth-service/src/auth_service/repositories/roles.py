"""Role repository."""
from __future__ import annotations

from uuid import UUID

from auth_service.core.exceptions import NotFoundError
from auth_service.domain.models import Role, ScopeType
from auth_service.repositories.base import BaseRepository

_ROLE_COLS = (
    "id, name, scope_type, project_id, is_builtin, description, "
    "created_by, created_at, updated_at"
)


class RoleRepository(BaseRepository):
    """Repository for role CRUD and role-permission mapping."""

    # ── CRUD ────────────────────────────────────────────────────────────

    async def create(
        self,
        name: str,
        scope_type: ScopeType,
        *,
        project_id: UUID | None = None,
        description: str | None = None,
        created_by: UUID | None = None,
    ) -> Role:
        """Create a custom role."""
        row = await self._fetchrow(
            f"INSERT INTO roles (name, scope_type, project_id, description, created_by) "
            f"VALUES ($1, $2, $3, $4, $5) RETURNING {_ROLE_COLS}",
            name, scope_type.value, project_id, description, created_by,
        )
        assert row is not None
        return Role.from_row(dict(row))

    async def get_by_id(self, role_id: UUID) -> Role | None:
        """Get role by ID."""
        row = await self._fetchrow(
            f"SELECT {_ROLE_COLS} FROM roles WHERE id = $1", role_id,
        )
        return Role.from_row(dict(row)) if row else None

    async def get_by_id_or_raise(self, role_id: UUID) -> Role:
        """Get role by ID or raise NotFoundError."""
        role = await self.get_by_id(role_id)
        if not role:
            raise NotFoundError(f"Role {role_id} not found")
        return role

    async def list_system(self) -> list[Role]:
        """List all system-scope roles."""
        rows = await self._fetch(
            f"SELECT {_ROLE_COLS} FROM roles "
            f"WHERE scope_type = 'system' ORDER BY is_builtin DESC, name",
        )
        return [Role.from_row(dict(r)) for r in rows]

    async def list_by_project(self, project_id: UUID) -> list[Role]:
        """List project-scope roles: built-in templates + custom for this project."""
        rows = await self._fetch(
            f"SELECT {_ROLE_COLS} FROM roles "
            f"WHERE scope_type = 'project' AND (project_id IS NULL OR project_id = $1) "
            f"ORDER BY is_builtin DESC, name",
            project_id,
        )
        return [Role.from_row(dict(r)) for r in rows]

    async def update(
        self,
        role_id: UUID,
        *,
        name: str | None = None,
        description: str | None = None,
    ) -> Role:
        """Update a custom role's name/description."""
        updates: list[str] = []
        params: list[object] = []
        idx = 1

        if name is not None:
            updates.append(f"name = ${idx}")
            params.append(name)
            idx += 1

        if description is not None:
            updates.append(f"description = ${idx}")
            params.append(description)
            idx += 1

        if not updates:
            return await self.get_by_id_or_raise(role_id)

        params.append(role_id)
        row = await self._fetchrow(
            f"UPDATE roles SET {', '.join(updates)}, updated_at = now() "
            f"WHERE id = ${idx} RETURNING {_ROLE_COLS}",
            *params,
        )
        if not row:
            raise NotFoundError(f"Role {role_id} not found")
        return Role.from_row(dict(row))

    async def delete(self, role_id: UUID) -> None:
        """Delete a role."""
        result = await self._execute("DELETE FROM roles WHERE id = $1", role_id)
        if result == "DELETE 0":
            raise NotFoundError(f"Role {role_id} not found")

    # ── Role ↔ Permission mapping ───────────────────────────────────────

    async def get_permissions(self, role_id: UUID) -> list[str]:
        """Get permission IDs assigned to a role."""
        rows = await self._fetch(
            "SELECT permission_id FROM role_permissions WHERE role_id = $1 ORDER BY permission_id",
            role_id,
        )
        return [row["permission_id"] for row in rows]

    async def set_permissions(self, role_id: UUID, permission_ids: list[str]) -> None:
        """Replace all permissions for a role (delete + insert)."""
        async with self._pool.acquire() as conn:
            async with conn.transaction():
                await conn.execute(
                    "DELETE FROM role_permissions WHERE role_id = $1", role_id,
                )
                if permission_ids:
                    await conn.executemany(
                        "INSERT INTO role_permissions (role_id, permission_id) VALUES ($1, $2)",
                        [(role_id, pid) for pid in permission_ids],
                    )
