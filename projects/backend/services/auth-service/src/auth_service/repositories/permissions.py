"""Permission repository."""
from __future__ import annotations

from auth_service.domain.models import Permission
from auth_service.repositories.base import BaseRepository


class PermissionRepository(BaseRepository):
    """Repository for permission catalog operations."""

    async def list_all(self) -> list[Permission]:
        """List all permissions."""
        rows = await self._fetch(
            "SELECT id, scope_type, category, description, created_at "
            "FROM permissions ORDER BY category, id"
        )
        return [Permission.from_row(dict(r)) for r in rows]

    async def get_by_ids(self, ids: list[str]) -> list[Permission]:
        """Get permissions by their IDs."""
        if not ids:
            return []
        rows = await self._fetch(
            "SELECT id, scope_type, category, description, created_at "
            "FROM permissions WHERE id = ANY($1::text[]) ORDER BY id",
            ids,
        )
        return [Permission.from_row(dict(r)) for r in rows]

    async def list_by_scope(self, scope_type: str) -> list[Permission]:
        """List permissions filtered by scope_type ('system' or 'project')."""
        rows = await self._fetch(
            "SELECT id, scope_type, category, description, created_at "
            "FROM permissions WHERE scope_type = $1 ORDER BY category, id",
            scope_type,
        )
        return [Permission.from_row(dict(r)) for r in rows]
