"""Run repository layer."""
from __future__ import annotations

import json
from typing import Any, List, Tuple
from uuid import UUID

from asyncpg import Pool, Record  # type: ignore[import-untyped]

from experiment_service.core.exceptions import NotFoundError
from experiment_service.domain.dto import RunCreateDTO, RunUpdateDTO
from experiment_service.domain.enums import RunStatus
from experiment_service.domain.models import Run
from experiment_service.repositories.base import BaseRepository


class RunRepository(BaseRepository):
    """CRUD helpers for runs."""

    JSONB_COLUMNS = {"params", "metadata"}

    def __init__(self, pool: Pool):
        super().__init__(pool)

    @staticmethod
    def _to_model(record: Record) -> Run:
        payload = dict(record)
        for column in RunRepository.JSONB_COLUMNS:
            value = payload.get(column)
            if isinstance(value, str):
                payload[column] = json.loads(value)
        return Run.model_validate(payload)

    async def create(self, data: RunCreateDTO) -> Run:
        query = """
            INSERT INTO runs (
                experiment_id,
                project_id,
                created_by,
                name,
                params,
                git_sha,
                env,
                notes,
                metadata,
                status,
                started_at,
                finished_at,
                duration_seconds
            )
            VALUES (
                $1,
                $2,
                $3,
                $4,
                $5::jsonb,
                $6,
                $7,
                $8,
                $9::jsonb,
                $10,
                $11,
                $12,
                $13
            )
            RETURNING *
        """
        record = await self._fetchrow(
            query,
            data.experiment_id,
            data.project_id,
            data.created_by,
            data.name,
            json.dumps(data.params),
            data.git_sha,
            data.env,
            data.notes,
            json.dumps(data.metadata),
            data.status.value,
            data.started_at,
            data.finished_at,
            data.duration_seconds,
        )
        assert record is not None
        return self._to_model(record)

    async def get(self, project_id: UUID, run_id: UUID) -> Run:
        record = await self._fetchrow(
            "SELECT * FROM runs WHERE project_id = $1 AND id = $2",
            project_id,
            run_id,
        )
        if record is None:
            raise NotFoundError("Run not found")
        return self._to_model(record)

    async def list_by_project(
        self, project_id: UUID, *, limit: int = 50, offset: int = 0
    ) -> Tuple[List[Run], int]:
        records = await self._fetch(
            """
            SELECT *,
                   COUNT(*) OVER() AS total_count
            FROM runs
            WHERE project_id = $1
            ORDER BY created_at DESC
            LIMIT $2 OFFSET $3
            """,
            project_id,
            limit,
            offset,
        )
        items: List[Run] = []
        total: int | None = None
        for rec in records:
            rec_dict = dict(rec)
            total_value = rec_dict.pop("total_count", None)
            if total_value is not None:
                total = int(total_value)
            for column in self.JSONB_COLUMNS:
                value = rec_dict.get(column)
                if isinstance(value, str):
                    rec_dict[column] = json.loads(value)
            items.append(Run.model_validate(rec_dict))
        if total is None:
            total = await self._count_by_project(project_id)
        return items, total

    async def list_by_experiment(
        self, project_id: UUID, experiment_id: UUID, *, limit: int = 50, offset: int = 0
    ) -> Tuple[List[Run], int]:
        records = await self._fetch(
            """
            SELECT *,
                   COUNT(*) OVER() AS total_count
            FROM runs
            WHERE project_id = $1 AND experiment_id = $2
            ORDER BY created_at DESC
            LIMIT $3 OFFSET $4
            """,
            project_id,
            experiment_id,
            limit,
            offset,
        )
        items: List[Run] = []
        total: int | None = None
        for rec in records:
            rec_dict = dict(rec)
            total_value = rec_dict.pop("total_count", None)
            if total_value is not None:
                total = int(total_value)
            for column in self.JSONB_COLUMNS:
                value = rec_dict.get(column)
                if isinstance(value, str):
                    rec_dict[column] = json.loads(value)
            items.append(Run.model_validate(rec_dict))
        if total is None:
            total = await self._count_by_experiment(project_id, experiment_id)
        return items, total

    async def _count_by_project(self, project_id: UUID) -> int:
        record = await self._fetchrow(
            "SELECT COUNT(*) AS total FROM runs WHERE project_id = $1",
            project_id,
        )
        return int(record["total"]) if record else 0

    async def _count_by_experiment(self, project_id: UUID, experiment_id: UUID) -> int:
        record = await self._fetchrow(
            "SELECT COUNT(*) AS total FROM runs WHERE project_id = $1 AND experiment_id = $2",
            project_id,
            experiment_id,
        )
        return int(record["total"]) if record else 0

    async def update(
        self,
        project_id: UUID,
        run_id: UUID,
        updates: RunUpdateDTO,
    ) -> Run:
        payload = updates.model_dump(exclude_none=True)
        if not payload:
            raise ValueError("No fields provided for update")

        assignments = []
        values: list[Any] = []
        idx = 1
        for column, value in payload.items():
            if column in self.JSONB_COLUMNS:
                column_expr = f"{column} = ${idx}::jsonb"
                values.append(json.dumps(value))  # type: ignore[arg-type]
            else:
                column_expr = f"{column} = ${idx}"
                values.append(value)
            assignments.append(column_expr)
            idx += 1
        assignments.append("updated_at = now()")
        values.extend([project_id, run_id])

        query = f"""
            UPDATE runs
            SET {', '.join(assignments)}
            WHERE project_id = ${idx} AND id = ${idx + 1}
            RETURNING *
        """
        record = await self._fetchrow(query, *values)
        if record is None:
            raise NotFoundError("Run not found")
        return self._to_model(record)

    async def delete(self, project_id: UUID, run_id: UUID) -> None:
        record = await self._fetchrow(
            "DELETE FROM runs WHERE project_id = $1 AND id = $2 RETURNING id",
            project_id,
            run_id,
        )
        if record is None:
            raise NotFoundError("Run not found")

    async def update_status_batch(
        self,
        project_id: UUID,
        run_ids: list[UUID],
        status: RunStatus,
    ) -> List[Run]:
        if not run_ids:
            return []
        async with self._pool.acquire() as conn, conn.transaction():
            records = await conn.fetch(
                """
                UPDATE runs
                SET status = $3, updated_at = now()
                WHERE project_id = $1 AND id = ANY($2::uuid[])
                RETURNING *
                """,
                project_id,
                run_ids,
                status.value,
            )
            if len(records) != len(run_ids):
                raise NotFoundError("One or more runs not found")
            return [self._to_model(record) for record in records]

