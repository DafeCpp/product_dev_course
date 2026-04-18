"""Revoked token repository."""
from __future__ import annotations

from datetime import datetime
from uuid import UUID

from auth_service.repositories.base import BaseRepository


class RevokedTokenRepository(BaseRepository):
    """Repository for revoked JWT tokens (blacklist)."""

    async def revoke(
        self,
        jti: UUID,
        user_id: UUID,
        expires_at: datetime,
        family_id: UUID | None = None,
    ) -> None:
        """Add a token jti to the blacklist."""
        query = """
            INSERT INTO revoked_tokens (jti, user_id, expires_at, family_id)
            VALUES ($1, $2, $3, $4)
            ON CONFLICT (jti) DO NOTHING
        """
        await self._execute(query, jti, user_id, expires_at, family_id)

    async def cleanup_expired(self) -> int:
        """Delete expired tokens and revoked families older than a threshold.

        Returns the number of rows deleted from revoked_tokens.
        """
        query = """
            DELETE FROM revoked_tokens
            WHERE expires_at < now()
        """
        result = await self._execute(query)
        # asyncpg returns "DELETE N" as string
        try:
            return int(str(result).split()[-1])
        except (IndexError, ValueError):
            return 0

    async def is_revoked(self, jti: UUID) -> bool:
        """Check whether a token jti is in the blacklist."""
        query = """
            SELECT EXISTS(
                SELECT 1 FROM revoked_tokens WHERE jti = $1
            )
        """
        row = await self._fetchrow(query, jti)
        return bool(row["exists"]) if row else False
