"""Revoked token repository."""
from __future__ import annotations

from datetime import datetime
from uuid import UUID

from auth_service.repositories.base import BaseRepository


class RevokedTokenRepository(BaseRepository):
    """Repository for revoked JWT tokens (blacklist)."""

    async def revoke(self, jti: UUID, user_id: UUID, expires_at: datetime) -> None:
        """Add a token jti to the blacklist."""
        query = """
            INSERT INTO revoked_tokens (jti, user_id, expires_at)
            VALUES ($1, $2, $3)
            ON CONFLICT (jti) DO NOTHING
        """
        await self._execute(query, jti, user_id, expires_at)

    async def is_revoked(self, jti: UUID) -> bool:
        """Check whether a token jti is in the blacklist."""
        query = """
            SELECT EXISTS(
                SELECT 1 FROM revoked_tokens WHERE jti = $1
            )
        """
        row = await self._fetchrow(query, jti)
        return bool(row["exists"]) if row else False
