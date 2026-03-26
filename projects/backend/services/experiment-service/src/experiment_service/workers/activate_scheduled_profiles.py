"""Worker task: activate draft profiles by valid_from, archive expired active profiles."""
from __future__ import annotations

from datetime import datetime

import structlog

from backend_common.db.pool import get_pool_service as get_pool

logger = structlog.get_logger(__name__)


async def activate_scheduled_profiles(now: datetime) -> str | None:
    """Activate DRAFT profiles whose valid_from <= now; archive ACTIVE ones past valid_to."""
    pool = await get_pool()

    async with pool.acquire() as conn:
        activated_rows: list[dict] = await conn.fetch(
            """
            UPDATE conversion_profiles
            SET status = 'active', updated_at = NOW()
            WHERE status = 'draft'
              AND valid_from IS NOT NULL
              AND valid_from <= $1
            RETURNING id
            """,
            now,
        )

        archived_rows: list[dict] = await conn.fetch(
            """
            UPDATE conversion_profiles
            SET status = 'archived', updated_at = NOW()
            WHERE status = 'active'
              AND valid_to IS NOT NULL
              AND valid_to < $1
            RETURNING id
            """,
            now,
        )

    activated = len(activated_rows)
    archived = len(archived_rows)

    if activated:
        for row in activated_rows:
            logger.info("profile_activated", profile_id=str(row["id"]))
    if archived:
        for row in archived_rows:
            logger.info("profile_archived", profile_id=str(row["id"]))

    if not activated and not archived:
        return None

    parts: list[str] = []
    if activated:
        parts.append(f"activated={activated}")
    if archived:
        parts.append(f"archived={archived}")
    return ", ".join(parts)
