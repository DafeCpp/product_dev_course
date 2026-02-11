"""Worker: auto-fail capture sessions stuck in running/backfilling."""
from __future__ import annotations

from datetime import datetime, timedelta

from backend_common.db.pool import get_pool_service as get_pool

from experiment_service.repositories.capture_sessions import CaptureSessionRepository
from experiment_service.settings import settings


async def stale_session_cleanup(now: datetime) -> str | None:
    """Mark running/backfilling sessions older than ``stale_session_max_hours`` as failed."""
    pool = await get_pool()
    cutoff = now - timedelta(hours=settings.stale_session_max_hours)
    failed = await CaptureSessionRepository(pool).fail_stale_sessions(cutoff)
    return f"failed_sessions={failed}" if failed else None
