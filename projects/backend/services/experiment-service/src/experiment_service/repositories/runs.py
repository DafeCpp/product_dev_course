"""Run repository layer."""
from __future__ import annotations

import json
from datetime import datetime
from typing import Any, List, Tuple
from uuid import UUID

from asyncpg import Pool, Record  # type: ignore[import-untyped]

from experiment_service.core.exceptions import NotFoundError
from experiment_service.domain.dto import RunCreateDTO, RunUpdateDTO
from experiment_service.domain.enums import RunStatus
from experiment_service.domain.models import Run, RunSensor
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
                duration_seconds,
                auto_complete_after_minutes
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
                $13,
                $14
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
            data.auto_complete_after_minutes,
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
        self,
        project_id: UUID,
        experiment_id: UUID,
        *,
        limit: int = 50,
        offset: int = 0,
        status: RunStatus | None = None,
        tags: list[str] | None = None,
        created_after: datetime | None = None,
        created_before: datetime | None = None,
    ) -> Tuple[List[Run], int]:
        conditions = ["project_id = $1", "experiment_id = $2"]
        params: list[Any] = [project_id, experiment_id]
        idx = 3
        if status is not None:
            conditions.append(f"status = ${idx}")
            params.append(status.value)
            idx += 1
        if tags:
            conditions.append(f"tags @> ${idx}")
            params.append(tags)
            idx += 1
        if created_after is not None:
            conditions.append(f"created_at >= ${idx}")
            params.append(created_after)
            idx += 1
        if created_before is not None:
            conditions.append(f"created_at <= ${idx}")
            params.append(created_before)
            idx += 1
        where = " AND ".join(conditions)
        params.extend([limit, offset])
        query = f"""
            SELECT *,
                   COUNT(*) OVER() AS total_count
            FROM runs
            WHERE {where}
            ORDER BY created_at DESC
            LIMIT ${idx} OFFSET ${idx + 1}
        """
        records = await self._fetch(query, *params)
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
            count_query = f"SELECT COUNT(*) AS total FROM runs WHERE {where}"
            count_params = params[: -(2)]
            record = await self._fetchrow(count_query, *count_params)
            total = int(record["total"]) if record else 0
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

    async def has_running_runs_for_experiment(
        self, project_id: UUID, experiment_id: UUID
    ) -> bool:
        """Return True if there is at least one run in 'running' status for the experiment."""
        record = await self._fetchrow(
            """
            SELECT 1
            FROM runs
            WHERE project_id = $1
              AND experiment_id = $2
              AND status = 'running'
            LIMIT 1
            """,
            project_id,
            experiment_id,
        )
        return record is not None

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

    async def fetch_runs_brief(
        self,
        project_id: UUID,
        experiment_id: UUID,
        run_ids: list[UUID],
    ) -> list[dict[str, Any]]:
        """Return id, name, status for a batch of run_ids in one query."""
        query = """
            SELECT id, name, status, experiment_id
            FROM runs
            WHERE project_id = $1
              AND experiment_id = $2
              AND id = ANY($3)
        """
        records = await self._fetch(query, project_id, experiment_id, run_ids)
        return [dict(r) for r in records]

    async def bulk_update_tags(
        self,
        project_id: UUID,
        run_ids: list[UUID],
        *,
        add_tags: list[str] | None = None,
        remove_tags: list[str] | None = None,
        set_tags: list[str] | None = None,
    ) -> List[Run]:
        if not run_ids:
            return []

        # De-duplicate while preserving order (mostly for len-check semantics)
        uniq_ids: list[UUID] = list(dict.fromkeys(run_ids))

        async with self._pool.acquire() as conn, conn.transaction():
            if set_tags is not None:
                records = await conn.fetch(
                    """
                    UPDATE runs
                    SET tags = $3::text[], updated_at = now()
                    WHERE project_id = $1 AND id = ANY($2::uuid[])
                    RETURNING *
                    """,
                    project_id,
                    uniq_ids,
                    set_tags,
                )
            else:
                add_tags = add_tags or []
                remove_tags = remove_tags or []
                records = await conn.fetch(
                    """
                    UPDATE runs
                    SET tags = (
                        SELECT COALESCE(array_agg(t ORDER BY t), '{}'::text[])
                        FROM (
                            SELECT DISTINCT t
                            FROM (
                                SELECT t
                                FROM unnest(runs.tags) AS t
                                WHERE NOT (t = ANY($4::text[]))
                                UNION ALL
                                SELECT t
                                FROM unnest($3::text[]) AS t
                            ) u
                        ) d
                    ),
                    updated_at = now()
                    WHERE project_id = $1 AND id = ANY($2::uuid[])
                    RETURNING *
                    """,
                    project_id,
                    uniq_ids,
                    add_tags,
                    remove_tags,
                )

            if len(records) != len(uniq_ids):
                raise NotFoundError("One or more runs not found")

            return [self._to_model(record) for record in records]

    async def get_overdue_runs(self, now: datetime) -> list[Run]:
        """Return running runs whose auto_complete deadline has passed."""
        records = await self._fetch(
            """
            SELECT * FROM runs
            WHERE status = 'running'
              AND auto_complete_after_minutes IS NOT NULL
              AND started_at + (auto_complete_after_minutes * interval '1 minute') <= $1
            """,
            now,
        )
        return [self._to_model(record) for record in records]

    async def list_sensors(self, run_id: UUID) -> list[RunSensor]:
        """List sensors attached to a run (not detached)."""
        rows = await self._fetch(
            """
            SELECT run_id, sensor_id, project_id, mode, attached_at, detached_at, created_by
            FROM run_sensors
            WHERE run_id = $1 AND detached_at IS NULL
            ORDER BY attached_at
            """,
            run_id,
        )
        return [RunSensor(**dict(row)) for row in rows]

    async def attach_sensor(
        self,
        run_id: UUID,
        sensor_id: UUID,
        project_id: UUID,
        created_by: UUID,
        mode: str = "passive",
    ) -> RunSensor:
        """Attach a sensor to a run (upsert — re-attaches if previously detached)."""
        row = await self._fetchrow(
            """
            INSERT INTO run_sensors (run_id, sensor_id, project_id, mode, attached_at, detached_at, created_by)
            VALUES ($1, $2, $3, $4, now(), NULL, $5)
            ON CONFLICT (run_id, sensor_id) DO UPDATE
                SET mode = EXCLUDED.mode,
                    attached_at = now(),
                    detached_at = NULL,
                    created_by = EXCLUDED.created_by
            RETURNING run_id, sensor_id, project_id, mode, attached_at, detached_at, created_by
            """,
            run_id, sensor_id, project_id, mode, created_by,
        )
        assert row is not None
        return RunSensor(**dict(row))

    async def detach_sensor(self, run_id: UUID, sensor_id: UUID) -> bool:
        """Detach a sensor from a run (soft delete). Returns True if row existed."""
        result = await self._execute(
            """
            UPDATE run_sensors SET detached_at = now()
            WHERE run_id = $1 AND sensor_id = $2 AND detached_at IS NULL
            """,
            run_id, sensor_id,
        )
        return result != "UPDATE 0"
