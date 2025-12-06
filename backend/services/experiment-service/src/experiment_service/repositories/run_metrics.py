"""Run metrics repository."""
from __future__ import annotations

from typing import Iterable, Sequence
from uuid import UUID

from asyncpg import Pool, Record  # type: ignore[import-untyped]

from experiment_service.domain.dto import RunMetricIngestDTO, RunMetricPointDTO
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
        """
        async with self._pool.acquire() as conn:
            await conn.executemany(query, values)

    async def fetch_series(
        self,
        project_id: UUID,
        run_id: UUID,
        *,
        name: str | None = None,
        from_step: int | None = None,
        to_step: int | None = None,
    ) -> list[RunMetric]:
        conditions = ["project_id = $1", "run_id = $2"]
        params: list = [project_id, run_id]
        idx = 3
        if name:
            conditions.append(f"name = ${idx}")
            params.append(name)
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
        query = f"""
            SELECT *
            FROM run_metrics
            WHERE {where_clause}
            ORDER BY name, step
        """
        rows: Iterable[Record] = await self._fetch(query, *params)
        return [self._to_model(row) for row in rows]

