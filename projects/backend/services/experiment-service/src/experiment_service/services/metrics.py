"""Run metrics service."""
from __future__ import annotations

import asyncio
from collections import defaultdict
from typing import List
from uuid import UUID

from experiment_service.core.exceptions import NotFoundError
from experiment_service.domain.dto import RunMetricIngestDTO, RunMetricPointDTO
from experiment_service.repositories.run_metrics import RunMetricsRepository
from experiment_service.repositories.runs import RunRepository

BATCH_LIMIT = 10_000
DEFAULT_LIMIT = 1_000
MAX_LIMIT = 10_000

MAX_COMPARISON_RUNS = 10
MAX_COMPARISON_METRICS = 20
DEFAULT_MAX_POINTS = 500


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

    async def compare_runs(
        self,
        project_id: UUID,
        experiment_id: UUID,
        *,
        run_ids: list[UUID],
        metric_names: list[str],
        from_step: int | None = None,
        to_step: int | None = None,
        max_points_per_series: int = DEFAULT_MAX_POINTS,
    ) -> dict:
        """Compare metrics across multiple runs in one call."""
        # 1. Validate constraints
        run_ids = list(dict.fromkeys(run_ids))  # deduplicate, preserve order
        if len(run_ids) < 2:
            raise ValueError("run_ids must contain at least 2 runs")
        if len(run_ids) > MAX_COMPARISON_RUNS:
            raise ValueError(f"run_ids must not exceed {MAX_COMPARISON_RUNS} runs")
        if not metric_names:
            raise ValueError("metric_names must contain at least one name")
        if len(metric_names) > MAX_COMPARISON_METRICS:
            raise ValueError(f"metric_names must not exceed {MAX_COMPARISON_METRICS} names")
        max_points_per_series = max(1, max_points_per_series)

        # 2. Fetch & validate runs belong to this experiment
        runs_brief = await self._run_repository.fetch_runs_brief(
            project_id, experiment_id, run_ids
        )
        found_ids = {r["id"] for r in runs_brief}
        missing = set(run_ids) - found_ids
        if missing:
            raise NotFoundError(f"Runs not found or not in experiment: {missing}")

        # 3. Fetch summary (agg + last in parallel), then count
        agg_rows, last_rows, count_rows = await asyncio.gather(
            self._metrics_repository.fetch_multi_run_summary(
                project_id, run_ids, metric_names
            ),
            self._metrics_repository.fetch_multi_run_last(
                project_id, run_ids, metric_names
            ),
            self._metrics_repository.count_multi_run_points(
                project_id, run_ids, metric_names,
                from_step=from_step, to_step=to_step,
            ),
        )

        # 4. Determine downsampling
        max_count = max((int(r["cnt"]) for r in count_rows), default=0)
        needs_downsampling = max_count > max_points_per_series

        if needs_downsampling:
            bucket_size = max(1, max_count // max_points_per_series)
            series_rows = await self._metrics_repository.fetch_multi_run_series_bucketed(
                project_id, run_ids, metric_names,
                bucket_size=bucket_size,
                from_step=from_step, to_step=to_step,
            )
        else:
            series_rows = await self._metrics_repository.fetch_multi_run_series(
                project_id, run_ids, metric_names,
                from_step=from_step, to_step=to_step,
            )

        # 5. Assemble response
        return self._build_comparison_response(
            experiment_id, runs_brief, metric_names,
            agg_rows, last_rows, series_rows,
        )

    def _build_comparison_response(
        self,
        experiment_id: UUID,
        runs_brief: list[dict],
        metric_names: list[str],
        agg_rows: list,
        last_rows: list,
        series_rows: list,
    ) -> dict:
        # Index summary data by (run_id, name)
        agg_by_key: dict[tuple, dict] = {}
        for row in agg_rows:
            key = (str(row["run_id"]), row["name"])
            agg_by_key[key] = {
                "total_steps": int(row["total_steps"]),
                "min": float(row["min_value"]),
                "max": float(row["max_value"]),
                "avg": float(row["avg_value"]),
            }

        last_by_key: dict[tuple, dict] = {}
        for row in last_rows:
            key = (str(row["run_id"]), row["name"])
            last_by_key[key] = {
                "last_step": int(row["last_step"]),
                "last_value": float(row["last_value"]),
            }

        # Index series by (run_id, name)
        series_by_key: dict[tuple, list[dict]] = defaultdict(list)
        for row in series_rows:
            key = (str(row["run_id"]), row["name"])
            series_by_key[key].append({
                "step": int(row["step"]),
                "value": float(row["value"]),
            })

        runs_out = []
        for run in runs_brief:
            run_id_str = str(run["id"])
            metrics_out: dict[str, dict] = {}
            for name in metric_names:
                key = (run_id_str, name)
                agg = agg_by_key.get(key)
                last = last_by_key.get(key)
                if agg is None and last is None:
                    # metric not logged for this run — skip key entirely
                    continue
                summary: dict | None = None
                if agg is not None:
                    summary = {
                        "last_value": last["last_value"] if last else None,
                        "last_step": last["last_step"] if last else None,
                        "min": agg["min"],
                        "max": agg["max"],
                        "avg": agg["avg"],
                        "count": agg["total_steps"],
                    }
                metrics_out[name] = {
                    "summary": summary,
                    "series": series_by_key.get(key, []),
                }
            runs_out.append({
                "run_id": run_id_str,
                "run_name": run.get("name"),
                "status": run.get("status"),
                "metrics": metrics_out,
            })

        return {
            "experiment_id": str(experiment_id),
            "metric_names": metric_names,
            "runs": runs_out,
        }

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
