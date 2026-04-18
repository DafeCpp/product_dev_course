"""Password reset token repository."""
from __future__ import annotations

from datetime import datetime
from uuid import UUID

import asyncpg  # type: ignore[import-untyped]

from auth_service.repositories.base import BaseRepository


class PasswordResetRepository(BaseRepository):
    """Repository for password reset token operations."""

    async def create_token(self, token: str, user_id: UUID, expires_at: datetime) -> None:
        """Create a password reset token, replacing any existing one for the user."""
        async with self._pool.acquire() as conn:
            async with conn.transaction():
                await conn.execute(
                    "DELETE FROM password_reset_tokens WHERE user_id = $1",
                    user_id,
                )
                await conn.execute(
                    "INSERT INTO password_reset_tokens (token, user_id, expires_at) VALUES ($1, $2, $3)",
                    token,
                    user_id,
                    expires_at,
                )

    async def get_by_token(self, token: str) -> asyncpg.Record | None:
        """Get password reset token record by token string."""
        return await self._fetchrow(
            "SELECT token, user_id, expires_at FROM password_reset_tokens WHERE token = $1",
            token,
        )

    async def delete_token(self, token: str) -> None:
        """Delete a password reset token."""
        await self._execute(
            "DELETE FROM password_reset_tokens WHERE token = $1",
            token,
        )
