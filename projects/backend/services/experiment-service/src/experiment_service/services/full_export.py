"""Full experiment export service — produces a ZIP archive for data science consumption."""
from __future__ import annotations

import csv
import io
import json
import zipfile
from typing import Any
from uuid import UUID

from asyncpg import Pool  # type: ignore[import-untyped]

from experiment_service.core.exceptions import NotFoundError
from experiment_service.repositories.artifacts import ArtifactRepository
from experiment_service.repositories.capture_sessions import CaptureSessionRepository
from experiment_service.repositories.experiments import ExperimentRepository
from experiment_service.repositories.run_metrics import RunMetricsRepository
from experiment_service.repositories.runs import RunRepository

try:
    import pyarrow as pa  # type: ignore[import-untyped,import-not-found]
    import pyarrow.parquet as pq  # type: ignore[import-untyped,import-not-found]

    HAS_PARQUET = True
except ImportError:  # pragma: no cover
    HAS_PARQUET = False

# ---------------------------------------------------------------------------
# Serialisation helpers
# ---------------------------------------------------------------------------

_TELEMETRY_COLUMNS = [
    "timestamp",
    "sensor_id",
    "signal",
    "raw_value",
    "physical_value",
    "conversion_status",
    "capture_session_id",
]

_METRICS_COLUMNS = ["name", "step", "value", "timestamp"]


def _to_parquet_or_csv(
    rows: list[dict[str, Any]],
    columns: list[str],
    *,
    ext_hint: str = "telemetry",
) -> tuple[bytes, str]:
    """Serialise *rows* (list of dicts) to Parquet or CSV.

    Returns ``(data_bytes, file_extension)``.
    """
    if HAS_PARQUET and rows:
        col_data: dict[str, list[Any]] = {col: [r.get(col) for r in rows] for col in columns}
        table = pa.table(col_data)
        buf = io.BytesIO()
        pq.write_table(table, buf)
        return buf.getvalue(), ".parquet"

    # CSV fallback
    buf_str = io.StringIO()
    writer = csv.writer(buf_str)
    writer.writerow(columns)
    for row in rows:
        writer.writerow([row.get(col, "") for col in columns])
    return buf_str.getvalue().encode(), ".csv"


def _serialize(value: Any) -> Any:
    """Convert asyncpg-returned values to JSON-serialisable types."""
    if hasattr(value, "isoformat"):
        return value.isoformat()
    if isinstance(value, UUID):
        return str(value)
    return value


def _record_to_dict(record: Any, columns: list[str]) -> dict[str, Any]:
    return {col: _serialize(record[col]) for col in columns}


# ---------------------------------------------------------------------------
# Service
# ---------------------------------------------------------------------------


class FullExportService:
    """Assembles a ZIP archive containing all data for a single experiment."""

    # Maximum rows fetched per telemetry session to keep memory bounded.
    _TELEMETRY_LIMIT = 500_000
    _METRICS_LIMIT = 100_000

    def __init__(self, pool: Pool) -> None:
        self._pool = pool
        self._exp_repo = ExperimentRepository(pool)
        self._run_repo = RunRepository(pool)
        self._cs_repo = CaptureSessionRepository(pool)
        self._metrics_repo = RunMetricsRepository(pool)
        self._artifact_repo = ArtifactRepository(pool)

    async def build_zip(self, project_id: UUID, experiment_id: UUID) -> bytes:
        """Build and return the ZIP archive as raw bytes."""

        # 1. Experiment metadata
        try:
            experiment = await self._exp_repo.get(project_id, experiment_id)
        except NotFoundError:
            raise

        exp_dir = f"experiment_{experiment_id}"

        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
            # experiment.json
            zf.writestr(
                f"{exp_dir}/experiment.json",
                json.dumps(experiment.model_dump(mode="json"), ensure_ascii=False, indent=2),
            )

            # 2. Runs
            runs, _ = await self._run_repo.list_by_experiment(
                project_id, experiment_id, limit=10_000, offset=0
            )

            all_sessions: list[Any] = []

            for run in runs:
                run_dir = f"{exp_dir}/runs/run_{run.id}"

                # run.json
                zf.writestr(
                    f"{run_dir}/run.json",
                    json.dumps(run.model_dump(mode="json"), ensure_ascii=False, indent=2),
                )

                # metrics
                await self._write_metrics(zf, project_id, run.id, run_dir)

                # telemetry per capture session
                sessions, _ = await self._cs_repo.list_by_run(
                    project_id, run.id, limit=1_000, offset=0
                )
                all_sessions.extend(sessions)

                for session in sessions:
                    await self._write_telemetry(zf, session.id, run_dir)

            # 3. capture_sessions.json
            zf.writestr(
                f"{exp_dir}/capture_sessions.json",
                json.dumps(
                    [s.model_dump(mode="json") for s in all_sessions],
                    ensure_ascii=False,
                    indent=2,
                ),
            )

            # 4. artifacts.json (metadata only, no file content)
            await self._write_artifacts(zf, runs, exp_dir)

        return buf.getvalue()

    # ------------------------------------------------------------------
    # Private helpers
    # ------------------------------------------------------------------

    async def _write_metrics(
        self,
        zf: zipfile.ZipFile,
        project_id: UUID,
        run_id: UUID,
        run_dir: str,
    ) -> None:
        metrics = await self._metrics_repo.fetch_series(
            project_id,
            run_id,
            limit=self._METRICS_LIMIT,
            offset=0,
        )
        if not metrics:
            return

        rows = [
            {
                "name": m.name,
                "step": m.step,
                "value": m.value,
                "timestamp": m.timestamp.isoformat() if m.timestamp else None,
            }
            for m in metrics
        ]
        data, ext = _to_parquet_or_csv(rows, _METRICS_COLUMNS)
        zf.writestr(f"{run_dir}/metrics{ext}", data)

    async def _write_telemetry(
        self,
        zf: zipfile.ZipFile,
        session_id: UUID,
        run_dir: str,
    ) -> None:
        query = """
            SELECT
                timestamp, sensor_id, signal,
                raw_value, physical_value,
                conversion_status, capture_session_id
            FROM telemetry_records
            WHERE capture_session_id = $1
            ORDER BY timestamp ASC
            LIMIT $2
        """
        async with self._pool.acquire() as conn:
            records = await conn.fetch(query, session_id, self._TELEMETRY_LIMIT)

        if not records:
            return

        rows = [_record_to_dict(r, _TELEMETRY_COLUMNS) for r in records]
        data, ext = _to_parquet_or_csv(rows, _TELEMETRY_COLUMNS)
        zf.writestr(f"{run_dir}/telemetry/sensor_{session_id}{ext}", data)

    async def _write_artifacts(
        self,
        zf: zipfile.ZipFile,
        runs: list[Any],
        exp_dir: str,
    ) -> None:
        all_artifacts: list[dict[str, Any]] = []
        for run in runs:
            artifacts, _ = await self._artifact_repo.list_by_run(
                run.id, limit=1_000, offset=0
            )
            all_artifacts.extend(a.model_dump(mode="json") for a in artifacts)

        zf.writestr(
            f"{exp_dir}/artifacts.json",
            json.dumps(all_artifacts, ensure_ascii=False, indent=2),
        )
