"""Worker: auto-complete runs that exceeded auto_complete_after_minutes."""
from __future__ import annotations

from datetime import datetime

import structlog

from backend_common.db.pool import get_pool_service as get_pool
from experiment_service.domain.enums import RunStatus
from experiment_service.repositories.runs import RunRepository

logger = structlog.get_logger(__name__)


async def auto_complete_runs(now: datetime) -> str | None:
    pool = await get_pool()
    repo = RunRepository(pool)
    overdue = await repo.get_overdue_runs(now)
    if not overdue:
        return None

    completed = 0
    for run in overdue:
        try:
            async with pool.acquire() as conn:
                await conn.execute(
                    """
                    UPDATE runs
                    SET status = $2, finished_at = $3,
                        duration_seconds = EXTRACT(EPOCH FROM ($3 - started_at))::integer,
                        updated_at = now()
                    WHERE id = $1 AND status = 'running'
                    """,
                    run.id,
                    RunStatus.SUCCEEDED.value,
                    now,
                )
            completed += 1
            logger.info("run_auto_completed", run_id=str(run.id))
        except Exception:
            logger.exception("run_auto_complete_failed", run_id=str(run.id))

    return f"completed={completed}" if completed else None
