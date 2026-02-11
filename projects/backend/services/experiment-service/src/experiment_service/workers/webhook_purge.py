"""Worker: purge old succeeded webhook deliveries."""
from __future__ import annotations

from datetime import datetime, timedelta

from backend_common.db.pool import get_pool_service as get_pool

from experiment_service.repositories.webhooks import WebhookDeliveryRepository
from experiment_service.settings import settings


async def webhook_purge_succeeded(now: datetime) -> str | None:
    """Delete succeeded deliveries older than ``webhook_succeeded_retention_days``."""
    pool = await get_pool()
    cutoff = now - timedelta(days=settings.webhook_succeeded_retention_days)
    purged = await WebhookDeliveryRepository(pool).delete_old_succeeded(cutoff)
    return f"purged={purged}" if purged else None
