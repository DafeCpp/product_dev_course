"""Worker: reclaim stuck webhook deliveries."""
from __future__ import annotations

from datetime import datetime, timedelta

from backend_common.db.pool import get_pool_service as get_pool

from experiment_service.repositories.webhooks import WebhookDeliveryRepository
from experiment_service.settings import settings


async def webhook_reclaim_stuck(now: datetime) -> str | None:
    """Release deliveries stuck in ``in_progress`` longer than ``webhook_stuck_minutes``."""
    pool = await get_pool()
    cutoff = now - timedelta(minutes=settings.webhook_stuck_minutes)
    reclaimed = await WebhookDeliveryRepository(pool).reclaim_stuck(cutoff)
    return f"reclaimed={reclaimed}" if reclaimed else None
