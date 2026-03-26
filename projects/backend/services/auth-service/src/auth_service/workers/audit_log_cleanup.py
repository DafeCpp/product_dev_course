"""Worker: delete old entries from audit_log."""
from __future__ import annotations

from datetime import datetime, timedelta

from backend_common.db.pool import get_pool_service as get_pool

from auth_service.settings import settings


async def audit_log_cleanup(now: datetime) -> str | None:
    """Delete audit_log entries older than ``audit_retention_days``."""
    pool = await get_pool()
    cutoff = now - timedelta(days=settings.audit_retention_days)

    async with pool.acquire() as conn:
        deleted: int = await conn.fetchval(
            """
            WITH deleted AS (
                DELETE FROM audit_log
                WHERE id IN (
                    SELECT id FROM audit_log WHERE timestamp < $1 LIMIT 1000
                )
                RETURNING id
            )
            SELECT count(*) FROM deleted
            """,
            cutoff,
        )

    if deleted:
        return f"deleted={deleted}"
    return None
