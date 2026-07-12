"""Worker: delete expired idempotency keys."""
from __future__ import annotations

from datetime import datetime

from backend_common.db.pool import get_pool_service as get_pool

from experiment_service.repositories.idempotency import IdempotencyRepository


async def idempotency_cleanup(now: datetime) -> str | None:
    """Delete idempotency records whose ``expires_at`` has passed.

    TTL is written into ``expires_at`` at reservation time, so the cutoff no
    longer has to be recomputed from ``idempotency_ttl_hours`` here.
    """
    pool = await get_pool()
    deleted = await IdempotencyRepository(pool).delete_expired()
    return f"deleted={deleted}" if deleted else None
