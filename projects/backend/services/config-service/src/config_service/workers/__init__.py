"""Background worker: cleanup expired idempotency keys."""
from __future__ import annotations

import asyncio

import structlog
from aiohttp import web

from backend_common.db.pool import get_pool
from config_service.repositories.idempotency_repo import IdempotencyRepository
from config_service.settings import settings

logger = structlog.get_logger(__name__)

_worker_task: asyncio.Task[None] | None = None


async def _cleanup_loop(_app: web.Application) -> None:
    while True:
        await asyncio.sleep(settings.worker_interval_seconds)
        try:
            pool = await get_pool()
            repo = IdempotencyRepository(pool)
            deleted = await repo.delete_expired()
            if deleted:
                logger.info("idempotency_keys_cleaned_up", count=deleted)
        except Exception:
            logger.exception("idempotency_cleanup_failed")


async def start_background_worker(app: web.Application) -> None:
    global _worker_task
    _worker_task = asyncio.create_task(_cleanup_loop(app))
    logger.info("background_worker_started")


async def stop_background_worker(_app: web.Application) -> None:
    global _worker_task
    if _worker_task is not None:
        _worker_task.cancel()
        try:
            await _worker_task
        except asyncio.CancelledError:
            pass
        _worker_task = None
    logger.info("background_worker_stopped")
