"""Capture session repository."""
from __future__ import annotations

from datetime import datetime
from typing import List, Tuple
from uuid import UUID

from asyncpg import Pool, Record  # type: ignore[import-untyped]

from experiment_service.core.exceptions import NotFoundError
from experiment_service.domain.dto import (
    CaptureSessionCreateDTO,
    CaptureSessionUpdateDTO,
)
from experiment_service.domain.models import CaptureSession
from experiment_service.repositories.base import BaseRepository


class CaptureSessionRepository(BaseRepository):
    """CRUD for capture sessions."""

    def __init__(self, pool: Pool):
        super().__init__(pool)

    @staticmethod
    def _to_model(record: Record) -> CaptureSession:
        return CaptureSession.model_validate(dict(record))

    async def create(self, data: CaptureSessionCreateDTO) -> CaptureSession:
        query = """
            INSERT INTO capture_sessions (
                run_id,
                project_id,
                ordinal_number,
                status,
                initiated_by,
                notes,
                started_at,
                stopped_at,
                archived
            )
            VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)
            RETURNING *
        """
        record = await self._fetchrow(
            query,
            data.run_id,
            data.project_id,
            data.ordinal_number,
            data.status.value,
            data.initiated_by,
            data.notes,
            data.started_at,
            data.stopped_at,
            data.archived,
        )
        assert record is not None
        return self._to_model(record)

    async def get(
        self, project_id: UUID, capture_session_id: UUID
    ) -> CaptureSession:
        record = await self._fetchrow(
            "SELECT * FROM capture_sessions WHERE project_id = $1 AND id = $2",
            project_id,
            capture_session_id,
        )
        if record is None:
            raise NotFoundError("Capture session not found")
        return self._to_model(record)

    async def list_by_project(
        self, project_id: UUID, *, limit: int = 50, offset: int = 0
    ) -> Tuple[List[CaptureSession], int]:
        records = await self._fetch(
            """
            SELECT *,
                   COUNT(*) OVER() AS total_count
            FROM capture_sessions
            WHERE project_id = $1
            ORDER BY created_at DESC
            LIMIT $2 OFFSET $3
            """,
            project_id,
            limit,
            offset,
        )
        items: List[CaptureSession] = []
        total: int | None = None
        for rec in records:
            rec_dict = dict(rec)
            total_value = rec_dict.pop("total_count", None)
            if total_value is not None:
                total = int(total_value)
            items.append(CaptureSession.model_validate(rec_dict))
        if total is None:
            total = await self._count_by_project(project_id)
        return items, total

    async def list_by_run(
        self, project_id: UUID, run_id: UUID, *, limit: int = 50, offset: int = 0
    ) -> Tuple[List[CaptureSession], int]:
        records = await self._fetch(
            """
            SELECT *,
                   COUNT(*) OVER() AS total_count
            FROM capture_sessions
            WHERE project_id = $1 AND run_id = $2
            ORDER BY ordinal_number ASC
            LIMIT $3 OFFSET $4
            """,
            project_id,
            run_id,
            limit,
            offset,
        )
        items: List[CaptureSession] = []
        total: int | None = None
        for rec in records:
            rec_dict = dict(rec)
            total_value = rec_dict.pop("total_count", None)
            if total_value is not None:
                total = int(total_value)
            items.append(CaptureSession.model_validate(rec_dict))
        if total is None:
            total = await self._count_by_run(project_id, run_id)
        return items, total

    async def _count_by_project(self, project_id: UUID) -> int:
        record = await self._fetchrow(
            "SELECT COUNT(*) AS total FROM capture_sessions WHERE project_id = $1",
            project_id,
        )
        return int(record["total"]) if record else 0

    async def _count_by_run(self, project_id: UUID, run_id: UUID) -> int:
        record = await self._fetchrow(
            "SELECT COUNT(*) AS total FROM capture_sessions WHERE project_id = $1 AND run_id = $2",
            project_id,
            run_id,
        )
        return int(record["total"]) if record else 0

    async def has_active_for_run(self, project_id: UUID, run_id: UUID) -> bool:
        """
        Active capture sessions are those that can still produce data and should block
        finishing/archiving the run.
        """
        record = await self._fetchrow(
            """
            SELECT 1
            FROM capture_sessions
            WHERE project_id = $1
              AND run_id = $2
              AND status IN ('draft', 'running', 'backfilling')
            LIMIT 1
            """,
            project_id,
            run_id,
        )
        return record is not None

    async def has_active_for_project(self, project_id: UUID) -> bool:
        """Active capture sessions in project (recording window)."""
        record = await self._fetchrow(
            """
            SELECT 1
            FROM capture_sessions
            WHERE project_id = $1
              AND status IN ('draft', 'running', 'backfilling')
            LIMIT 1
            """,
            project_id,
        )
        return record is not None

    async def update(
        self,
        project_id: UUID,
        capture_session_id: UUID,
        updates: CaptureSessionUpdateDTO,
    ) -> CaptureSession:
        payload = updates.model_dump(exclude_none=True)
        if not payload:
            raise ValueError("No fields provided for update")

        assignments = []
        values = []
        idx = 1
        for column, value in payload.items():
            assignments.append(f"{column} = ${idx}")
            values.append(value)
            idx += 1
        assignments.append("updated_at = now()")
        values.extend([project_id, capture_session_id])

        query = f"""
            UPDATE capture_sessions
            SET {', '.join(assignments)}
            WHERE project_id = ${idx} AND id = ${idx + 1}
            RETURNING *
        """
        record = await self._fetchrow(query, *values)
        if record is None:
            raise NotFoundError("Capture session not found")
        return self._to_model(record)

    async def fail_stale_sessions(self, started_before: datetime) -> int:
        """Mark running/backfilling sessions as ``failed`` if started before *started_before*.

        Returns the number of updated rows.
        """
        result = await self._execute(
            """
            UPDATE capture_sessions
            SET status = 'failed',
                stopped_at = now(),
                updated_at = now()
            WHERE status IN ('running', 'backfilling')
              AND started_at < $1
            """,
            started_before,
        )
        return int(result.split()[-1])

    async def delete(self, project_id: UUID, capture_session_id: UUID) -> None:
        record = await self._fetchrow(
            "DELETE FROM capture_sessions WHERE project_id = $1 AND id = $2 RETURNING id",
            project_id,
            capture_session_id,
        )
        if record is None:
            raise NotFoundError("Capture session not found")

