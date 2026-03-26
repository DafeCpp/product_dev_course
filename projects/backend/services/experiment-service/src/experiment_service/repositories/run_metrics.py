"""Run metrics repository."""
from __future__ import annotations

from typing import Any, Iterable
from uuid import UUID

from asyncpg import Pool, Record  # type: ignore[import-untyped]

from experiment_service.domain.dto import RunMetricIngestDTO
from experiment_service.domain.models import RunMetric
from experiment_service.repositories.base import BaseRepository


class RunMetricsRepository(BaseRepository):
    """Stores and fetches run metrics."""

    def __init__(self, pool: Pool):
        super().__init__(pool)

    @staticmethod
    def _to_model(record: Record) -> RunMetric:
        return RunMetric.model_validate(dict(record))

    async def bulk_insert(self, payload: RunMetricIngestDTO) -> None:
        values = [
            (
                payload.project_id,
                payload.run_id,
                point.name,
                point.step,
                point.value,
                point.timestamp,
            )
            for point in payload.points
        ]
        query = """
            INSERT INTO run_metrics (
                project_id,
                run_id,
                name,
                step,
                value,
                timestamp
            )
            VALUES ($1, $2, $3, $4, $5, $6)
            ON CONFLICT DO NOTHING
        """
        async with self._pool.acquire() as conn:
            await conn.executemany(query, values)

    async def fetch_series(
        self,
        project_id: UUID,
        run_id: UUID,
        *,
        name: str | None = None,
        names: list[str] | None = None,
        from_step: int | None = None,
        to_step: int | None = None,
        order: str = "asc",
        limit: int = 1000,
        offset: int = 0,
    ) -> list[RunMetric]:
        conditions = ["project_id = $1", "run_id = $2"]
        params: list[Any] = [project_id, run_id]
        idx = 3

        if name is not None and names is None:
            names = [name]

        if names:
            conditions.append(f"name = ANY(${idx})")
            params.append(names)
            idx += 1
        if from_step is not None:
            conditions.append(f"step >= ${idx}")
            params.append(from_step)
            idx += 1
        if to_step is not None:
            conditions.append(f"step <= ${idx}")
            params.append(to_step)
            idx += 1

        order_dir = "DESC" if order.lower() == "desc" else "ASC"
        where_clause = " AND ".join(conditions)
        query = f"""
            SELECT id, project_id, run_id, name, step, value, timestamp, created_at
            FROM run_metrics
            WHERE {where_clause}
            ORDER BY name, step {order_dir}
            LIMIT ${idx} OFFSET ${idx + 1}
        """
        params.extend([limit, offset])
        rows: Iterable[Record] = await self._fetch(query, *params)
        return [self._to_model(row) for row in rows]

    async def count_series(
        self,
        project_id: UUID,
        run_id: UUID,
        *,
        name: str | None = None,
        names: list[str] | None = None,
        from_step: int | None = None,
        to_step: int | None = None,
    ) -> int:
        conditions = ["project_id = $1", "run_id = $2"]
        params: list[Any] = [project_id, run_id]
        idx = 3

        if name is not None and names is None:
            names = [name]

        if names:
            conditions.append(f"name = ANY(${idx})")
            params.append(names)
            idx += 1
        if from_step is not None:
            conditions.append(f"step >= ${idx}")
            params.append(from_step)
            idx += 1
        if to_step is not None:
            conditions.append(f"step <= ${idx}")
            params.append(to_step)
            idx += 1

        where_clause = " AND ".join(conditions)
        query = f"SELECT count(*)::bigint FROM run_metrics WHERE {where_clause}"
        row = await self._fetchrow(query, *params)
        return int(row[0]) if row is not None else 0

    async def fetch_summary_aggregates(
        self,
        project_id: UUID,
        run_id: UUID,
        *,
        names: list[str] | None = None,
    ) -> list[Record]:
        conditions = ["project_id = $1", "run_id = $2"]
        params: list[Any] = [project_id, run_id]
        idx = 3

        if names:
            conditions.append(f"name = ANY(${idx})")
            params.append(names)
            idx += 1

        where_clause = " AND ".join(conditions)
        query = f"""
            SELECT
                name,
                count(*)::bigint AS total_steps,
                min(value)       AS min_value,
                max(value)       AS max_value,
                avg(value)       AS avg_value
            FROM run_metrics
            WHERE {where_clause}
            GROUP BY name
            ORDER BY name
        """
        rows: list[Record] = list(await self._fetch(query, *params))
        return rows

    async def fetch_last_per_metric(
        self,
        project_id: UUID,
        run_id: UUID,
        *,
        names: list[str] | None = None,
    ) -> list[Record]:
        conditions = ["project_id = $1", "run_id = $2"]
        params: list[Any] = [project_id, run_id]
        idx = 3

        if names:
            conditions.append(f"name = ANY(${idx})")
            params.append(names)
            idx += 1

        where_clause = " AND ".join(conditions)
        query = f"""
            SELECT DISTINCT ON (name)
                name,
                step        AS last_step,
                value       AS last_value,
                timestamp   AS last_timestamp
            FROM run_metrics
            WHERE {where_clause}
            ORDER BY name, step DESC
        """
        rows: list[Record] = list(await self._fetch(query, *params))
        return rows

    async def fetch_multi_run_summary(
        self,
        project_id: UUID,
        run_ids: list[UUID],
        names: list[str],
    ) -> list[Record]:
        """Return min/max/avg/count aggregates grouped by (run_id, name)."""
        query = """
            SELECT
                run_id,
                name,
                count(*)::bigint  AS total_steps,
                min(value)        AS min_value,
                max(value)        AS max_value,
                avg(value)        AS avg_value
            FROM run_metrics
            WHERE project_id = $1
              AND run_id = ANY($2)
              AND name = ANY($3)
            GROUP BY run_id, name
            ORDER BY run_id, name
        """
        rows: list[Record] = list(await self._fetch(query, project_id, run_ids, names))
        return rows

    async def fetch_multi_run_last(
        self,
        project_id: UUID,
        run_ids: list[UUID],
        names: list[str],
    ) -> list[Record]:
        """Return last (by step) value per (run_id, name)."""
        query = """
            SELECT DISTINCT ON (run_id, name)
                run_id,
                name,
                step  AS last_step,
                value AS last_value
            FROM run_metrics
            WHERE project_id = $1
              AND run_id = ANY($2)
              AND name = ANY($3)
            ORDER BY run_id, name, step DESC
        """
        rows: list[Record] = list(await self._fetch(query, project_id, run_ids, names))
        return rows

    async def count_multi_run_points(
        self,
        project_id: UUID,
        run_ids: list[UUID],
        names: list[str],
        *,
        from_step: int | None = None,
        to_step: int | None = None,
    ) -> list[Record]:
        """Return COUNT per (run_id, name) with optional step range filter."""
        conditions = ["project_id = $1", "run_id = ANY($2)", "name = ANY($3)"]
        params: list[Any] = [project_id, run_ids, names]
        idx = 4
        if from_step is not None:
            conditions.append(f"step >= ${idx}")
            params.append(from_step)
            idx += 1
        if to_step is not None:
            conditions.append(f"step <= ${idx}")
            params.append(to_step)
            idx += 1
        where_clause = " AND ".join(conditions)
        query = f"""
            SELECT run_id, name, count(*)::bigint AS cnt
            FROM run_metrics
            WHERE {where_clause}
            GROUP BY run_id, name
        """
        rows: list[Record] = list(await self._fetch(query, *params))
        return rows

    async def fetch_multi_run_series(
        self,
        project_id: UUID,
        run_ids: list[UUID],
        names: list[str],
        *,
        from_step: int | None = None,
        to_step: int | None = None,
    ) -> list[Record]:
        """Return raw (run_id, name, step, value) rows ordered by run_id, name, step."""
        conditions = ["project_id = $1", "run_id = ANY($2)", "name = ANY($3)"]
        params: list[Any] = [project_id, run_ids, names]
        idx = 4
        if from_step is not None:
            conditions.append(f"step >= ${idx}")
            params.append(from_step)
            idx += 1
        if to_step is not None:
            conditions.append(f"step <= ${idx}")
            params.append(to_step)
            idx += 1
        where_clause = " AND ".join(conditions)
        query = f"""
            SELECT run_id, name, step, value
            FROM run_metrics
            WHERE {where_clause}
            ORDER BY run_id, name, step
        """
        rows: list[Record] = list(await self._fetch(query, *params))
        return rows

    async def fetch_multi_run_series_bucketed(
        self,
        project_id: UUID,
        run_ids: list[UUID],
        names: list[str],
        *,
        bucket_size: int,
        from_step: int | None = None,
        to_step: int | None = None,
    ) -> list[Record]:
        """Return step-bucketed avg(value) per (run_id, name, bucket)."""
        conditions = ["project_id = $1", "run_id = ANY($2)", "name = ANY($3)"]
        # $4 is bucket_size — referenced in SELECT expressions
        params: list[Any] = [project_id, run_ids, names, bucket_size]
        idx = 5
        if from_step is not None:
            conditions.append(f"step >= ${idx}")
            params.append(from_step)
            idx += 1
        if to_step is not None:
            conditions.append(f"step <= ${idx}")
            params.append(to_step)
            idx += 1
        where_clause = " AND ".join(conditions)
        query = f"""
            SELECT
                run_id,
                name,
                (step / $4) * $4 AS step,
                avg(value)        AS value
            FROM run_metrics
            WHERE {where_clause}
            GROUP BY run_id, name, (step / $4)
            ORDER BY run_id, name, step
        """
        rows: list[Record] = list(await self._fetch(query, *params))
        return rows

    async def fetch_aggregations(
        self,
        project_id: UUID,
        run_id: UUID,
        *,
        names: list[str],
        from_step: int | None = None,
        to_step: int | None = None,
        bucket_size: int,
    ) -> list[Record]:
        conditions = ["project_id = $1", "run_id = $2", "name = ANY($3)"]
        params: list[Any] = [project_id, run_id, names, bucket_size]
        idx = 5

        if from_step is not None:
            conditions.append(f"step >= ${idx}")
            params.append(from_step)
            idx += 1
        if to_step is not None:
            conditions.append(f"step <= ${idx}")
            params.append(to_step)
            idx += 1

        where_clause = " AND ".join(conditions)
        # $4 is bucket_size used in SELECT expressions
        query = f"""
            SELECT
                name,
                (step / $4) * $4          AS step_from,
                (step / $4) * $4 + $4 - 1 AS step_to,
                min(value)                 AS min_val,
                avg(value)                 AS avg_val,
                max(value)                 AS max_val,
                count(*)::bigint           AS cnt
            FROM run_metrics
            WHERE {where_clause}
            GROUP BY name, (step / $4)
            ORDER BY name, step_from
        """
        rows: list[Record] = list(await self._fetch(query, *params))
        return rows
