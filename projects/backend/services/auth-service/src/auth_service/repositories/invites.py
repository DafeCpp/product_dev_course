"""Invite token repository."""
from __future__ import annotations

from datetime import datetime
from uuid import UUID

from auth_service.domain.models import InviteToken
from backend_common.repositories.base import BaseRepository


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

    async def claim(self, token: UUID) -> InviteToken | None:
        """Atomically claim an unused, unexpired invite token.

        Sets ``used_at`` only if the token is still unused and not expired.
        Returns the updated token, or ``None`` if another caller already
        claimed it (or it expired). This conditional ``UPDATE`` is the
        single-use serialization point: concurrent registrations racing on the
        same token will see exactly one ``RETURNING`` row.
        """
        row = await self._fetchrow(
            """
            UPDATE invite_tokens
            SET used_at = now()
            WHERE token = $1 AND used_at IS NULL AND expires_at > now()
            RETURNING id, token, created_by, email_hint, expires_at, used_at, used_by, created_at
            """,
            token,
        )
        return InviteToken.from_row(dict(row)) if row else None

    async def mark_used(self, token: UUID, user_id: UUID) -> InviteToken:
        """Record which user consumed an already-claimed invite token."""
        row = await self._fetchrow(
            """
            UPDATE invite_tokens
            SET used_by = $2
            WHERE token = $1
            RETURNING id, token, created_by, email_hint, expires_at, used_at, used_by, created_at
            """,
            token,
            user_id,
        )
        assert row is not None
        return InviteToken.from_row(dict(row))

    async def unclaim(self, token: UUID) -> None:
        """Release a previously claimed invite when user creation failed.

        Only resets used_at when used_by IS NULL (no user was ever linked),
        so a successfully consumed invite can never be recycled.
        """
        await self._execute(
            """
            UPDATE invite_tokens
            SET used_at = NULL
            WHERE token = $1 AND used_by IS NULL
            """,
            token,
        )

    async def claim_and_assign(self, token: UUID, user_id: UUID) -> InviteToken | None:
        """Atomically claim an invite and record its consumer in one UPDATE.

        Sets both ``used_at`` and ``used_by`` only if the token is still unused
        and not expired. No intermediate state is possible: either both fields are
        set (success) or neither is touched (another caller won the race or the
        token expired). Returns ``None`` if the token could not be claimed.
        """
        row = await self._fetchrow(
            """
            UPDATE invite_tokens
            SET used_at = now(), used_by = $2
            WHERE token = $1 AND used_at IS NULL AND expires_at > now()
            RETURNING id, token, created_by, email_hint, expires_at, used_at, used_by, created_at
            """,
            token,
            user_id,
        )
        return InviteToken.from_row(dict(row)) if row else None

    async def delete(self, token: UUID) -> bool:
        """Delete an invite token. Returns True if a row was deleted."""
        result = await self._execute(
            "DELETE FROM invite_tokens WHERE token = $1",
            token,
        )
        # asyncpg returns "DELETE N" where N is the row count
        return result.endswith("1")
