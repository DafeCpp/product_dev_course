"""Worker: clean up expired revoked tokens and stale token families."""
from __future__ import annotations

from datetime import datetime

from backend_common.db.pool import get_pool_service as get_pool


async def token_cleanup(now: datetime) -> str | None:
    """Delete expired revoked_tokens and revoked families with no live tokens."""
    pool = await get_pool()

    async with pool.acquire() as conn:
        # 1. Delete revoked_tokens that have already expired.
        deleted_tokens: int = await conn.fetchval(
            """
            WITH deleted AS (
                DELETE FROM revoked_tokens
                WHERE expires_at < $1
                RETURNING jti
            )
            SELECT count(*) FROM deleted
            """,
            now,
        )

        # 2. Delete revoked families that no longer have any tokens referencing them.
        deleted_families: int = await conn.fetchval(
            """
            WITH deleted AS (
                DELETE FROM refresh_token_families
                WHERE revoked_at IS NOT NULL
                  AND NOT EXISTS (
                      SELECT 1 FROM revoked_tokens rt WHERE rt.family_id = refresh_token_families.id
                  )
                RETURNING id
            )
            SELECT count(*) FROM deleted
            """,
        )

    parts: list[str] = []
    if deleted_tokens:
        parts.append(f"revoked_tokens={deleted_tokens}")
    if deleted_families:
        parts.append(f"families={deleted_families}")
    return ", ".join(parts) if parts else None
