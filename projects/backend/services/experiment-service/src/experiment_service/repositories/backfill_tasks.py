"""Repository for conversion backfill tasks."""
from __future__ import annotations

from datetime import datetime
from typing import Any
from uuid import UUID

from backend_common.repositories.base import BaseRepository


class BackfillTaskRepository(BaseRepository):
    """CRUD for ``conversion_backfill_tasks``."""

    async def create(
        self,
        *,
        sensor_id: UUID,
        project_id: UUID,
        conversion_profile_id: UUID,
        created_by: UUID,
    ) -> dict[str, Any]:
        row = await self._fetchrow(
            """
            INSERT INTO conversion_backfill_tasks
                (sensor_id, project_id, conversion_profile_id, created_by)
            VALUES ($1, $2, $3, $4)
            RETURNING *
            """,
            sensor_id,
            project_id,
            conversion_profile_id,
            created_by,
        )
        return dict(row)  # type: ignore[arg-type]

    async def get(self, task_id: UUID) -> dict[str, Any] | None:
        row = await self._fetchrow(
            "SELECT * FROM conversion_backfill_tasks WHERE id = $1",
            task_id,
        )
        return dict(row) if row else None

    async def list_by_sensor(
        self, sensor_id: UUID, *, limit: int = 20, offset: int = 0
    ) -> tuple[list[dict[str, Any]], int]:
        count_row = await self._fetchrow(
            "SELECT count(*) AS cnt FROM conversion_backfill_tasks WHERE sensor_id = $1",
            sensor_id,
        )
        total = int(count_row["cnt"]) if count_row else 0  # type: ignore[index]

        rows = await self._fetch(
            """
            SELECT * FROM conversion_backfill_tasks
            WHERE sensor_id = $1
            ORDER BY created_at DESC
            LIMIT $2 OFFSET $3
            """,
            sensor_id,
            limit,
            offset,
        )
        return [dict(r) for r in rows], total

    async def claim_pending(self) -> dict[str, Any] | None:
        """Atomically pick one pending task and mark it as running."""
        row = await self._fetchrow(
            """
            UPDATE conversion_backfill_tasks
            SET status = 'running', started_at = now()
            WHERE id = (
                SELECT id FROM conversion_backfill_tasks
                WHERE status = 'pending'
                ORDER BY created_at ASC
                LIMIT 1
                FOR UPDATE SKIP LOCKED
            )
            RETURNING *
            """,
        )
        return dict(row) if row else None

    async def set_total(self, task_id: UUID, total: int) -> None:
        await self._execute(
            "UPDATE conversion_backfill_tasks SET total_records = $2 WHERE id = $1",
            task_id,
            total,
        )

    async def update_progress(self, task_id: UUID, processed: int) -> None:
        await self._execute(
            "UPDATE conversion_backfill_tasks SET processed_records = $2 WHERE id = $1",
            task_id,
            processed,
        )

    async def mark_completed(self, task_id: UUID, processed: int) -> None:
        await self._execute(
            """
            UPDATE conversion_backfill_tasks
            SET status = 'completed', processed_records = $2, completed_at = now()
            WHERE id = $1
            """,
            task_id,
            processed,
        )

    async def mark_failed(self, task_id: UUID, error_message: str) -> None:
        await self._execute(
            """
            UPDATE conversion_backfill_tasks
            SET status = 'failed', error_message = $2, completed_at = now()
            WHERE id = $1
            """,
            task_id,
            error_message,
        )
