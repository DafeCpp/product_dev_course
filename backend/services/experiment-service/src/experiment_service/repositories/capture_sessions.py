"""Capture session repository."""
from __future__ import annotations

from typing import List
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
    ) -> List[CaptureSession]:
        records = await self._fetch(
            """
            SELECT *
            FROM capture_sessions
            WHERE project_id = $1
            ORDER BY created_at DESC
            LIMIT $2 OFFSET $3
            """,
            project_id,
            limit,
            offset,
        )
        return [self._to_model(rec) for rec in records]

    async def list_by_run(
        self, project_id: UUID, run_id: UUID, *, limit: int = 50, offset: int = 0
    ) -> List[CaptureSession]:
        records = await self._fetch(
            """
            SELECT *
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
        return [self._to_model(rec) for rec in records]

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

    async def delete(self, project_id: UUID, capture_session_id: UUID) -> None:
        record = await self._fetchrow(
            "DELETE FROM capture_sessions WHERE project_id = $1 AND id = $2 RETURNING id",
            project_id,
            capture_session_id,
        )
        if record is None:
            raise NotFoundError("Capture session not found")

