"""Worker: delete old audit log entries from run_events and capture_session_events."""
from __future__ import annotations

from datetime import datetime, timedelta

from backend_common.db.pool import get_pool_service as get_pool

from experiment_service.settings import settings


async def audit_log_cleanup(now: datetime) -> str | None:
    """Delete run_events and capture_session_events older than ``audit_retention_days``."""
    pool = await get_pool()
    cutoff = now - timedelta(days=settings.audit_retention_days)

    async with pool.acquire() as conn:
        deleted_run: int = await conn.fetchval(
            """
            WITH deleted AS (
                DELETE FROM run_events
                WHERE id IN (
                    SELECT id FROM run_events WHERE created_at < $1 LIMIT 1000
                )
                RETURNING id
            )
            SELECT count(*) FROM deleted
            """,
            cutoff,
        )

        deleted_session: int = await conn.fetchval(
            """
            WITH deleted AS (
                DELETE FROM capture_session_events
                WHERE id IN (
                    SELECT id FROM capture_session_events WHERE created_at < $1 LIMIT 1000
                )
                RETURNING id
            )
            SELECT count(*) FROM deleted
            """,
            cutoff,
        )

    total = (deleted_run or 0) + (deleted_session or 0)
    if total:
        return (
            f"deleted_run_events={deleted_run or 0}"
            f" deleted_capture_session_events={deleted_session or 0}"
        )
    return None
