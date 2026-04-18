"""Token family repository for refresh token rotation."""
from __future__ import annotations

from uuid import UUID

from auth_service.repositories.base import BaseRepository


class TokenFamilyRepository(BaseRepository):
    """Repository for refresh token families (rotation support)."""

    async def create(self, user_id: UUID) -> UUID:
        """Create a new token family and return its id."""
        query = """
            INSERT INTO refresh_token_families (user_id)
            VALUES ($1)
            RETURNING id
        """
        row = await self._fetchrow(query, user_id)
        if row is None:
            raise RuntimeError("Failed to create token family")
        return UUID(str(row["id"]))

    async def is_revoked(self, family_id: UUID) -> bool:
        """Return True if the family has been revoked (revoked_at IS NOT NULL)."""
        query = """
            SELECT EXISTS(
                SELECT 1 FROM refresh_token_families
                WHERE id = $1 AND revoked_at IS NOT NULL
            )
        """
        row = await self._fetchrow(query, family_id)
        return bool(row["exists"]) if row else False

    async def revoke_family(self, family_id: UUID) -> None:
        """Revoke a single token family."""
        query = """
            UPDATE refresh_token_families
            SET revoked_at = now()
            WHERE id = $1 AND revoked_at IS NULL
        """
        await self._execute(query, family_id)

    async def revoke_all_user_families(self, user_id: UUID) -> None:
        """Revoke all active token families for a user (e.g. on password change)."""
        query = """
            UPDATE refresh_token_families
            SET revoked_at = now()
            WHERE user_id = $1 AND revoked_at IS NULL
        """
        await self._execute(query, user_id)
