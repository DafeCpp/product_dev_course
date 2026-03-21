"""Background worker: flush spooled telemetry batches to PostgreSQL.

The worker runs as a long-lived asyncio task.  On each tick it:
1. Lists all spool files in chronological order (oldest first).
2. Tries to replay each file via ``TelemetryIngestService._flush_spool_record``.
3. Deletes successfully replayed files.
4. Stops the current cycle on the first ``PostgresError`` — if the DB
   is still unavailable there is no point trying the remaining files.

Corrupted spool files (JSON parse error, unexpected schema) are renamed to
``.bad`` to avoid blocking the queue indefinitely.
"""
from __future__ import annotations

import asyncio
from pathlib import Path

from asyncpg.exceptions import PostgresError  # type: ignore[import-untyped]
import structlog

from backend_common.db.pool import get_pool_service as get_pool
from telemetry_ingest_service.services.spool import delete_spool, list_spool_files, read_spool
from telemetry_ingest_service.services.telemetry import TelemetryIngestService
from telemetry_ingest_service.settings import settings

logger = structlog.get_logger(__name__)


async def _flush_one(path: Path) -> bool:
    """Flush a single spool file.

    Returns ``True`` when the file was processed (success or bad file that was
    quarantined).  Returns ``False`` when the DB is still unavailable, which
    signals the caller to abort the current cycle.
    """
    try:
        record = read_spool(path)
    except Exception as exc:
        logger.error("spool_read_error", path=str(path), error=str(exc))
        path.rename(path.with_suffix(".bad"))
        return True  # quarantined — skip and continue with the next file

    service = TelemetryIngestService()
    pool = await get_pool()
    try:
        async with pool.acquire() as conn:
            async with conn.transaction():
                await service._flush_spool_record(conn, record)
        delete_spool(path)
        logger.info("spool_flushed", path=str(path), items=len(record.items))
        return True
    except PostgresError as exc:
        logger.warning("spool_flush_db_unavailable", error=str(exc))
        return False


async def run_spool_flush_worker(app=None) -> None:
    """Periodically replay spooled batches until the task is cancelled."""
    log = logger.bind(worker="spool_flush")
    log.info("spool_flush_worker_started", interval=settings.spool_flush_interval_seconds)

    while True:
        await asyncio.sleep(settings.spool_flush_interval_seconds)

        files = list_spool_files()
        if not files:
            continue

        log.info("spool_flush_cycle_start", pending=len(files))
        flushed = 0

        for path in files:
            ok = await _flush_one(path)
            if ok:
                flushed += 1
            else:
                break  # DB still down — wait for the next tick

        if flushed:
            log.info(
                "spool_flush_cycle_done",
                flushed=flushed,
                remaining=len(files) - flushed,
            )
