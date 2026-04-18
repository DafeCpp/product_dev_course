"""Background worker task: auto-activate scheduled conversion profiles.

Finds SCHEDULED conversion profiles whose ``valid_from <= now`` and
activates them, deprecating any currently ACTIVE profile for the same
sensor (same logic as ``publish_profile`` in the repository).
"""
from __future__ import annotations

from datetime import datetime

import structlog

from backend_common.db.pool import get_pool_service as get_pool
from experiment_service.domain.enums import ConversionProfileStatus

logger = structlog.get_logger(__name__)


async def scheduled_profile_activation(now: datetime) -> str | None:
    """Activate SCHEDULED profiles whose valid_from <= now."""
    pool = await get_pool()

    async with pool.acquire() as conn:
        due = await conn.fetch(
            """
            SELECT id, project_id, sensor_id
            FROM conversion_profiles
            WHERE status = $1 AND valid_from <= $2
            """,
            ConversionProfileStatus.SCHEDULED.value,
            now,
        )

    if not due:
        return None

    activated = 0
    for row in due:
        profile_id = row["id"]
        project_id = row["project_id"]
        sensor_id = row["sensor_id"]
        try:
            async with pool.acquire() as conn:
                async with conn.transaction():
                    # Atomically claim: only update if still SCHEDULED
                    result = await conn.execute(
                        """
                        UPDATE conversion_profiles
                        SET status = $2, updated_at = now()
                        WHERE id = $1 AND status = $3
                        """,
                        profile_id,
                        ConversionProfileStatus.ACTIVE.value,
                        ConversionProfileStatus.SCHEDULED.value,
                    )
                    if result == "UPDATE 0":
                        continue

                    # Deprecate any currently ACTIVE profile for same sensor
                    await conn.execute(
                        """
                        UPDATE conversion_profiles
                        SET status = $4, valid_to = now(), updated_at = now()
                        WHERE sensor_id = $1
                          AND project_id = $2
                          AND status = $3
                          AND id <> $5
                        """,
                        sensor_id,
                        project_id,
                        ConversionProfileStatus.ACTIVE.value,
                        ConversionProfileStatus.DEPRECATED.value,
                        profile_id,
                    )

                    # Update sensor's active_profile_id
                    await conn.execute(
                        """
                        UPDATE sensors
                        SET active_profile_id = $3, updated_at = now()
                        WHERE project_id = $1 AND id = $2
                        """,
                        project_id,
                        sensor_id,
                        profile_id,
                    )

            activated += 1
            logger.info(
                "scheduled_profile_activated",
                profile_id=str(profile_id),
                sensor_id=str(sensor_id),
            )
        except Exception:
            logger.exception(
                "scheduled_profile_activation_failed",
                profile_id=str(profile_id),
                sensor_id=str(sensor_id),
            )

    return f"activated={activated}" if activated else None
