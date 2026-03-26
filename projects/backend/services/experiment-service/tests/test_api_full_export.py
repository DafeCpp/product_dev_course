"""Unit tests for full experiment export (ZIP)."""
from __future__ import annotations

import io
import json
import uuid
import zipfile
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Any
from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from aiohttp import web

from experiment_service.services.full_export import FullExportService


# ---------------------------------------------------------------------------
# Fake domain objects
# ---------------------------------------------------------------------------

@dataclass
class _FakeObj:
    """Minimal fake that supports model_dump(mode='json')."""

    _data: dict = field(default_factory=dict)

    def model_dump(self, *, mode: str = "python") -> dict:
        return self._data


def _experiment(eid: uuid.UUID) -> _FakeObj:
    return _FakeObj(_data={
        "id": str(eid),
        "name": "Test Experiment",
        "metadata": {"source": "unit"},
    })


def _run(rid: uuid.UUID) -> _FakeObj:
    obj = _FakeObj(_data={
        "id": str(rid),
        "name": "Run 1",
        "params": {"lr": 0.001},
    })
    obj.id = rid  # type: ignore[attr-defined]
    return obj


@dataclass
class _FakeMetric:
    name: str
    step: int
    value: float
    timestamp: datetime | None = None


@dataclass
class _FakeArtifact:
    _data: dict = field(default_factory=dict)

    def model_dump(self, *, mode: str = "python") -> dict:
        return self._data


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

async def _build(
    *,
    experiment: _FakeObj | None = None,
    runs: list[_FakeObj] | None = None,
    metrics: list[_FakeMetric] | None = None,
    sessions: list[_FakeObj] | None = None,
    artifacts: list[_FakeArtifact] | None = None,
    telemetry_records: list[Any] | None = None,
) -> bytes:
    project_id = uuid.uuid4()
    experiment_id = uuid.uuid4()

    if experiment is None:
        experiment = _experiment(experiment_id)
    if runs is None:
        runs = []
    if sessions is None:
        sessions = []
    if artifacts is None:
        artifacts = []
    if metrics is None:
        metrics = []

    pool = MagicMock()
    svc = FullExportService(pool)

    # Patch repositories
    svc._exp_repo = MagicMock()
    svc._exp_repo.get = AsyncMock(return_value=experiment)

    svc._run_repo = MagicMock()
    svc._run_repo.list_by_experiment = AsyncMock(return_value=(runs, len(runs)))

    svc._cs_repo = MagicMock()
    svc._cs_repo.list_by_run = AsyncMock(return_value=(sessions, len(sessions)))

    svc._metrics_repo = MagicMock()
    svc._metrics_repo.fetch_series = AsyncMock(return_value=metrics)

    svc._artifact_repo = MagicMock()
    svc._artifact_repo.list_by_run = AsyncMock(return_value=(artifacts, len(artifacts)))

    # Mock telemetry query (pool.acquire)
    conn = AsyncMock()
    conn.fetch = AsyncMock(return_value=telemetry_records or [])

    class _AcquireCM:
        async def __aenter__(self):
            return conn
        async def __aexit__(self, *a):
            pass

    pool.acquire = lambda: _AcquireCM()

    return await svc.build_zip(project_id, experiment_id)


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_export_returns_valid_zip():
    data = await _build()
    with zipfile.ZipFile(io.BytesIO(data)) as zf:
        assert zf.testzip() is None


@pytest.mark.asyncio
async def test_export_contains_experiment_json():
    eid = uuid.uuid4()
    exp = _experiment(eid)
    data = await _build(experiment=exp)

    with zipfile.ZipFile(io.BytesIO(data)) as zf:
        names = zf.namelist()
        exp_files = [n for n in names if n.endswith("experiment.json")]
        assert len(exp_files) == 1
        parsed = json.loads(zf.read(exp_files[0]))
        assert parsed["name"] == "Test Experiment"


@pytest.mark.asyncio
async def test_export_contains_run_data():
    rid = uuid.uuid4()
    run = _run(rid)
    metrics = [_FakeMetric(name="loss", step=1, value=0.5)]

    data = await _build(runs=[run], metrics=metrics)

    with zipfile.ZipFile(io.BytesIO(data)) as zf:
        names = zf.namelist()
        run_files = [n for n in names if "run.json" in n]
        assert len(run_files) == 1
        parsed = json.loads(zf.read(run_files[0]))
        assert parsed["params"]["lr"] == 0.001

        metrics_files = [n for n in names if "metrics" in n and "run" in n]
        assert len(metrics_files) == 1


@pytest.mark.asyncio
async def test_export_contains_capture_sessions_json():
    data = await _build()
    with zipfile.ZipFile(io.BytesIO(data)) as zf:
        cs_files = [n for n in zf.namelist() if "capture_sessions.json" in n]
        assert len(cs_files) == 1
        parsed = json.loads(zf.read(cs_files[0]))
        assert isinstance(parsed, list)


@pytest.mark.asyncio
async def test_export_contains_artifacts_json():
    data = await _build()
    with zipfile.ZipFile(io.BytesIO(data)) as zf:
        art_files = [n for n in zf.namelist() if "artifacts.json" in n]
        assert len(art_files) == 1
        parsed = json.loads(zf.read(art_files[0]))
        assert isinstance(parsed, list)


@pytest.mark.asyncio
async def test_export_multiple_runs():
    rid1, rid2 = uuid.uuid4(), uuid.uuid4()
    runs = [_run(rid1), _run(rid2)]

    data = await _build(runs=runs)

    with zipfile.ZipFile(io.BytesIO(data)) as zf:
        run_jsons = [n for n in zf.namelist() if "run.json" in n]
        assert len(run_jsons) == 2


@pytest.mark.asyncio
async def test_export_no_metrics_skips_file():
    rid = uuid.uuid4()
    run = _run(rid)

    data = await _build(runs=[run], metrics=[])

    with zipfile.ZipFile(io.BytesIO(data)) as zf:
        metrics_files = [n for n in zf.namelist() if "metrics" in n and "run" in n]
        assert len(metrics_files) == 0


@pytest.mark.asyncio
async def test_export_csv_fallback():
    """Without pyarrow, metrics should be written as CSV."""
    rid = uuid.uuid4()
    run = _run(rid)
    metrics = [_FakeMetric(name="loss", step=1, value=0.5)]

    with patch("experiment_service.services.full_export.HAS_PARQUET", False):
        data = await _build(runs=[run], metrics=metrics)

    with zipfile.ZipFile(io.BytesIO(data)) as zf:
        csv_files = [n for n in zf.namelist() if n.endswith(".csv")]
        assert len(csv_files) >= 1
