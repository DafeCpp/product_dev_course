"""Run metrics service."""
from __future__ import annotations

from collections import defaultdict
from datetime import datetime
from typing import Dict, List
from uuid import UUID

from experiment_service.core.exceptions import NotFoundError
from experiment_service.domain.dto import RunMetricIngestDTO, RunMetricPointDTO
from experiment_service.domain.models import RunMetric
from experiment_service.repositories.run_metrics import RunMetricsRepository
from experiment_service.repositories.runs import RunRepository


class MetricsService:
    """Handles run metrics ingestion and retrieval."""

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
        from_step: int | None = None,
        to_step: int | None = None,
    ) -> dict:
        await self._run_repository.get(project_id, run_id)
        rows = await self._metrics_repository.fetch_series(
            project_id,
            run_id,
            name=name,
            from_step=from_step,
            to_step=to_step,
        )
        series: Dict[str, list[dict]] = defaultdict(list)
        for row in rows:
            series[row.name].append(
                {
                    "step": row.step,
                    "value": row.value,
                    "timestamp": row.timestamp.isoformat(),
                }
            )
        return {
            "run_id": str(run_id),
            "series": [
                {"name": metric_name, "points": points}
                for metric_name, points in series.items()
            ],
        }

