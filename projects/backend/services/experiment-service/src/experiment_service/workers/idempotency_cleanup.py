"""Worker: delete expired idempotency keys."""
from __future__ import annotations

from datetime import datetime, timedelta

from backend_common.db.pool import get_pool_service as get_pool

from experiment_service.repositories.idempotency import IdempotencyRepository
from experiment_service.settings import settings


async def idempotency_cleanup(now: datetime) -> str | None:
    """Delete idempotency records older than ``idempotency_ttl_hours``."""
    pool = await get_pool()
    cutoff = now - timedelta(hours=settings.idempotency_ttl_hours)
    deleted = await IdempotencyRepository(pool).delete_expired(cutoff)
    return f"deleted={deleted}" if deleted else None
