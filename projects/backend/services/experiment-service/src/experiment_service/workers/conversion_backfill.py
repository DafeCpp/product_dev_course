"""Background worker task: process conversion backfill jobs.

Picks pending backfill tasks and recomputes ``physical_value`` for
telemetry records using the specified conversion profile.
"""
from __future__ import annotations

import json
from datetime import datetime
from uuid import UUID

import structlog

from backend_common.conversion import apply_conversion
from backend_common.db.pool import get_pool_service as get_pool
from experiment_service.repositories.backfill_tasks import BackfillTaskRepository

logger = structlog.get_logger(__name__)

BATCH_SIZE = 1000


async def conversion_backfill(now: datetime) -> str | None:
    """Process one pending backfill task (called periodically by BackgroundWorker)."""
    pool = await get_pool()
    repo = BackfillTaskRepository(pool)

    task = await repo.claim_pending()
    if task is None:
        return None

    task_id: UUID = task["id"]
    sensor_id: UUID = task["sensor_id"]
    profile_id: UUID = task["conversion_profile_id"]

    logger.info("backfill_started", task_id=str(task_id), sensor_id=str(sensor_id))

    try:
        # Load the conversion profile.
        async with pool.acquire() as conn:
            profile_row = await conn.fetchrow(
                "SELECT kind, payload FROM conversion_profiles WHERE id = $1",
                profile_id,
            )
        if profile_row is None:
            await repo.mark_failed(task_id, "Conversion profile not found")
            return f"task={task_id} failed: profile not found"

        kind: str = profile_row["kind"]
        payload = profile_row["payload"]
        if isinstance(payload, str):
            payload = json.loads(payload)

        # Count records to process.
        async with pool.acquire() as conn:
            count_row = await conn.fetchrow(
                """
                SELECT count(*) AS cnt FROM telemetry_records
                WHERE sensor_id = $1
                  AND (conversion_profile_id IS DISTINCT FROM $2
                       OR conversion_status IN ('raw_only', 'conversion_failed'))
                """,
                sensor_id,
                profile_id,
            )
        total = int(count_row["cnt"]) if count_row else 0
        await repo.set_total(task_id, total)

        if total == 0:
            await repo.mark_completed(task_id, 0)
            return f"task={task_id} completed: 0 records"

        processed = 0
        last_ts = datetime.min
        last_id = 0

        while processed < total:
            async with pool.acquire() as conn:
                rows = await conn.fetch(
                    """
                    SELECT id, timestamp, raw_value
                    FROM telemetry_records
                    WHERE sensor_id = $1
                      AND (conversion_profile_id IS DISTINCT FROM $2
                           OR conversion_status IN ('raw_only', 'conversion_failed'))
                      AND (timestamp, id) > ($3, $4)
                    ORDER BY timestamp ASC, id ASC
                    LIMIT $5
                    """,
                    sensor_id,
                    profile_id,
                    last_ts,
                    last_id,
                    BATCH_SIZE,
                )

            if not rows:
                break

            updates: list[tuple] = []
            for row in rows:
                raw_value = float(row["raw_value"])
                result = apply_conversion(kind, payload, raw_value)
                if result is not None:
                    updates.append((result, "converted", profile_id, row["id"], sensor_id, row["timestamp"]))
                else:
                    updates.append((None, "conversion_failed", profile_id, row["id"], sensor_id, row["timestamp"]))
                last_ts = row["timestamp"]
                last_id = int(row["id"])

            if updates:
                async with pool.acquire() as conn:
                    await conn.executemany(
                        """
                        UPDATE telemetry_records
                        SET physical_value = $1,
                            conversion_status = $2,
                            conversion_profile_id = $3
                        WHERE id = $4 AND sensor_id = $5 AND timestamp = $6
                        """,
                        updates,
                    )

            processed += len(rows)
            await repo.update_progress(task_id, processed)

        await repo.mark_completed(task_id, processed)
        logger.info("backfill_completed", task_id=str(task_id), processed=processed)
        return f"task={task_id} completed: {processed} records"

    except Exception as exc:
        logger.exception("backfill_failed", task_id=str(task_id))
        await repo.mark_failed(task_id, str(exc)[:500])
        return f"task={task_id} failed: {exc}"
