"""Run metrics service."""
from __future__ import annotations

from collections import defaultdict
from typing import List
from uuid import UUID

from experiment_service.domain.dto import RunMetricIngestDTO, RunMetricPointDTO
from experiment_service.repositories.run_metrics import RunMetricsRepository
from experiment_service.repositories.runs import RunRepository

BATCH_LIMIT = 10_000
DEFAULT_LIMIT = 1_000
MAX_LIMIT = 10_000


class MetricsService:
    """Handles run metrics ingestion and retrieval."""

    BATCH_LIMIT = BATCH_LIMIT

    def __init__(
        self,
        run_repository: RunRepository,
        metrics_repository: RunMetricsRepository,
    ):
        self._run_repository = run_repository
        self._metrics_repository = metrics_repository

    async def ingest_metrics(
        self,
        project_id: UUID,
        run_id: UUID,
        points: List[RunMetricPointDTO],
    ) -> int:
        if len(points) > self.BATCH_LIMIT:
            raise ValueError(
                f"Batch too large: {len(points)} points exceed limit of {self.BATCH_LIMIT}"
            )
        await self._run_repository.get(project_id, run_id)
        payload = RunMetricIngestDTO(project_id=project_id, run_id=run_id, points=points)
        await self._metrics_repository.bulk_insert(payload)
        return len(points)

    async def query_metrics(
        self,
        project_id: UUID,
        run_id: UUID,
        *,
        name: str | None = None,
        names: list[str] | None = None,
        from_step: int | None = None,
        to_step: int | None = None,
        order: str = "asc",
        limit: int = DEFAULT_LIMIT,
        offset: int = 0,
    ) -> dict:
        limit = min(limit, MAX_LIMIT)
        await self._run_repository.get(project_id, run_id)
        total = await self._metrics_repository.count_series(
            project_id,
            run_id,
            name=name,
            names=names,
            from_step=from_step,
            to_step=to_step,
        )
        rows = await self._metrics_repository.fetch_series(
            project_id,
            run_id,
            name=name,
            names=names,
            from_step=from_step,
            to_step=to_step,
            order=order,
            limit=limit,
            offset=offset,
        )
        series: dict[str, list[dict]] = defaultdict(list)
        for row in rows:
            series[row.name].append(
                {
                    "step": row.step,
                    "value": row.value,
                    "timestamp": row.timestamp.isoformat() if row.timestamp else None,
                }
            )
        return {
            "run_id": str(run_id),
            "series": [
                {"name": metric_name, "points": points}
                for metric_name, points in series.items()
            ],
            "total": total,
            "limit": limit,
            "offset": offset,
        }

    async def get_summary(
        self,
        project_id: UUID,
        run_id: UUID,
        *,
        names: list[str] | None = None,
    ) -> dict:
        await self._run_repository.get(project_id, run_id)

        agg_rows = await self._metrics_repository.fetch_summary_aggregates(
            project_id, run_id, names=names
        )
        last_rows = await self._metrics_repository.fetch_last_per_metric(
            project_id, run_id, names=names
        )

        last_by_name: dict[str, dict] = {
            row["name"]: {
                "last_step": row["last_step"],
                "last_value": float(row["last_value"]),
                "last_timestamp": (
                    row["last_timestamp"].isoformat() if row["last_timestamp"] else None
                ),
            }
            for row in last_rows
        }

        metrics = []
        for row in agg_rows:
            metric_name: str = row["name"]
            last = last_by_name.get(metric_name, {})
            metrics.append(
                {
                    "name": metric_name,
                    "last_step": last.get("last_step"),
                    "last_value": last.get("last_value"),
                    "last_timestamp": last.get("last_timestamp"),
                    "total_steps": int(row["total_steps"]),
                    "min_value": float(row["min_value"]),
                    "max_value": float(row["max_value"]),
                    "avg_value": float(row["avg_value"]),
                }
            )

        return {"run_id": str(run_id), "metrics": metrics}

    async def get_aggregations(
        self,
        project_id: UUID,
        run_id: UUID,
        *,
        names: list[str],
        from_step: int | None = None,
        to_step: int | None = None,
        bucket_size: int | None = None,
    ) -> dict:
        await self._run_repository.get(project_id, run_id)

        if bucket_size is None:
            if from_step is not None and to_step is not None and to_step > from_step:
                bucket_size = max(1, (to_step - from_step) // 300)
            else:
                bucket_size = 1

        rows = await self._metrics_repository.fetch_aggregations(
            project_id,
            run_id,
            names=names,
            from_step=from_step,
            to_step=to_step,
            bucket_size=bucket_size,
        )

        series_map: dict[str, list[dict]] = defaultdict(list)
        for row in rows:
            series_map[row["name"]].append(
                {
                    "step_from": int(row["step_from"]),
                    "step_to": int(row["step_to"]),
                    "min": float(row["min_val"]),
                    "avg": float(row["avg_val"]),
                    "max": float(row["max_val"]),
                    "count": int(row["cnt"]),
                }
            )

        return {
            "run_id": str(run_id),
            "bucket_size": bucket_size,
            "series": [
                {"name": metric_name, "buckets": buckets}
                for metric_name, buckets in series_map.items()
            ],
        }
