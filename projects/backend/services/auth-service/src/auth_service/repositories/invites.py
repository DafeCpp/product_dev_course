"""Invite token repository."""
from __future__ import annotations

from datetime import datetime
from uuid import UUID

from auth_service.domain.models import InviteToken
from auth_service.repositories.base import BaseRepository


class InviteRepository(BaseRepository):
    """Repository for invite token operations."""

    async def create(
        self,
        created_by: UUID,
        email_hint: str | None,
        expires_at: datetime,
    ) -> InviteToken:
        """Create a new invite token."""
        row = await self._fetchrow(
            """
            INSERT INTO invite_tokens (created_by, email_hint, expires_at)
            VALUES ($1, $2, $3)
            RETURNING id, token, created_by, email_hint, expires_at, used_at, used_by, created_at
            """,
            created_by,
            email_hint,
            expires_at,
        )
        assert row is not None
        return InviteToken.from_row(dict(row))

    async def get_by_token(self, token: UUID) -> InviteToken | None:
        """Get an invite token by its UUID token value."""
        row = await self._fetchrow(
            """
            SELECT id, token, created_by, email_hint, expires_at, used_at, used_by, created_at
            FROM invite_tokens
            WHERE token = $1
            """,
            token,
        )
        return InviteToken.from_row(dict(row)) if row else None

    async def list_all(self, active_only: bool = False) -> list[InviteToken]:
        """List all invite tokens, optionally filtering to active ones only."""
        if active_only:
            rows = await self._fetch(
                """
                SELECT id, token, created_by, email_hint, expires_at, used_at, used_by, created_at
                FROM invite_tokens
                WHERE used_at IS NULL AND expires_at > now()
                ORDER BY created_at DESC
                """
            )
        else:
            rows = await self._fetch(
                """
                SELECT id, token, created_by, email_hint, expires_at, used_at, used_by, created_at
                FROM invite_tokens
                ORDER BY created_at DESC
                """
            )
        return [InviteToken.from_row(dict(row)) for row in rows]

    async def mark_used(self, token: UUID, user_id: UUID) -> InviteToken:
        """Mark an invite token as used by the given user."""
        row = await self._fetchrow(
            """
            UPDATE invite_tokens
            SET used_at = now(), used_by = $2
            WHERE token = $1
            RETURNING id, token, created_by, email_hint, expires_at, used_at, used_by, created_at
            """,
            token,
            user_id,
        )
        assert row is not None
        return InviteToken.from_row(dict(row))

    async def delete(self, token: UUID) -> bool:
        """Delete an invite token. Returns True if a row was deleted."""
        result = await self._execute(
            "DELETE FROM invite_tokens WHERE token = $1",
            token,
        )
        # asyncpg returns "DELETE N" where N is the row count
        return result.endswith("1")
