"""Worker task: activate draft profiles by valid_from, deprecate expired active profiles."""
from __future__ import annotations

from datetime import datetime

import structlog

from backend_common.db.pool import get_pool_service as get_pool

logger = structlog.get_logger(__name__)


async def activate_scheduled_profiles(now: datetime) -> str | None:
    """Activate DRAFT profiles whose valid_from <= now; deprecate ACTIVE ones past valid_to."""
    pool = await get_pool()

    async with pool.acquire() as conn:
        activated_rows: list[dict] = await conn.fetch(
            """
            UPDATE conversion_profiles
            SET status = 'active', updated_at = NOW()
            WHERE status = 'draft'
              AND valid_from IS NOT NULL
              AND valid_from <= $1
            RETURNING id, sensor_id, project_id
            """,
            now,
        )

        deprecated_rows: list[dict] = await conn.fetch(
            """
            UPDATE conversion_profiles
            SET status = 'deprecated', updated_at = NOW()
            WHERE status = 'active'
              AND valid_to IS NOT NULL
              AND valid_to < $1
            RETURNING id
            """,
            now,
        )

    activated = len(activated_rows)
    deprecated = len(deprecated_rows)

    if activated:
        for row in activated_rows:
            logger.info("profile_activated", profile_id=str(row["id"]))
            async with pool.acquire() as conn:
                await conn.execute(
                    """
                    UPDATE sensors
                    SET active_profile_id = $3, updated_at = NOW()
                    WHERE project_id = $1 AND id = $2
                    """,
                    row["project_id"],
                    row["sensor_id"],
                    row["id"],
                )
    if deprecated:
        for row in deprecated_rows:
            logger.info("profile_deprecated", profile_id=str(row["id"]))

    if not activated and not deprecated:
        return None

    parts: list[str] = []
    if activated:
        parts.append(f"activated={activated}")
    if deprecated:
        parts.append(f"deprecated={deprecated}")
    return ", ".join(parts)
