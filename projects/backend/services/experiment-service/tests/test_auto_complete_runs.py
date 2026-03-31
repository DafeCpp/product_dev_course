"""Tests for the auto_complete_runs worker and the auto_complete_after_minutes field."""
from __future__ import annotations

import uuid
from datetime import datetime, timedelta, timezone
from unittest.mock import AsyncMock, MagicMock, patch

import pytest

from tests.utils import make_headers


# ---------------------------------------------------------------------------
# Helper
# ---------------------------------------------------------------------------

class _AsyncContextManager:
    def __init__(self, value: object) -> None:
        self._value = value

    async def __aenter__(self) -> object:
        return self._value

    async def __aexit__(self, *_: object) -> None:
        pass


def _make_run(run_id: uuid.UUID, started_at: datetime) -> MagicMock:
    run = MagicMock()
    run.id = run_id
    run.started_at = started_at
    return run


def _make_pool_with_overdue(runs: list) -> AsyncMock:
    """Pool mock: fetch returns overdue runs, execute succeeds."""
    conn = AsyncMock()
    conn.execute = AsyncMock(return_value=None)

    pool = AsyncMock()
    pool.acquire = MagicMock(return_value=_AsyncContextManager(conn))
    return pool, conn


# ---------------------------------------------------------------------------
# Worker unit tests (no DB)
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_auto_complete_runs_completes_overdue() -> None:
    """Worker completes overdue running runs."""
    run_id = uuid.uuid4()
    now = datetime.now(timezone.utc)
    run = _make_run(run_id, now - timedelta(minutes=61))

    pool, conn = _make_pool_with_overdue([run])

    with patch(
        "experiment_service.workers.auto_complete_runs.get_pool",
        new_callable=AsyncMock,
        return_value=pool,
    ), patch(
        "experiment_service.repositories.runs.RunRepository.get_overdue_runs",
        new_callable=AsyncMock,
        return_value=[run],
    ):
        from experiment_service.workers.auto_complete_runs import auto_complete_runs

        result = await auto_complete_runs(now)

    assert result == "completed=1"
    conn.execute.assert_called_once()


@pytest.mark.asyncio
async def test_auto_complete_runs_returns_none_when_no_overdue() -> None:
    """Worker returns None when there are no overdue runs."""
    now = datetime.now(timezone.utc)
    pool, _ = _make_pool_with_overdue([])

    with patch(
        "experiment_service.workers.auto_complete_runs.get_pool",
        new_callable=AsyncMock,
        return_value=pool,
    ), patch(
        "experiment_service.repositories.runs.RunRepository.get_overdue_runs",
        new_callable=AsyncMock,
        return_value=[],
    ):
        from experiment_service.workers.auto_complete_runs import auto_complete_runs

        result = await auto_complete_runs(now)

    assert result is None


@pytest.mark.asyncio
async def test_auto_complete_runs_skips_not_yet_due() -> None:
    """Worker does not complete runs whose deadline has not passed yet."""
    now = datetime.now(timezone.utc)
    pool, conn = _make_pool_with_overdue([])

    with patch(
        "experiment_service.workers.auto_complete_runs.get_pool",
        new_callable=AsyncMock,
        return_value=pool,
    ), patch(
        "experiment_service.repositories.runs.RunRepository.get_overdue_runs",
        new_callable=AsyncMock,
        return_value=[],
    ):
        from experiment_service.workers.auto_complete_runs import auto_complete_runs

        result = await auto_complete_runs(now)

    assert result is None
    conn.execute.assert_not_called()


def test_worker_has_auto_complete_runs_task() -> None:
    from experiment_service.workers import worker

    task_names = [t.name for t in worker.tasks]
    assert "auto_complete_runs" in task_names


# ---------------------------------------------------------------------------
# API integration tests
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_create_run_with_auto_complete_after_minutes(service_client):
    """auto_complete_after_minutes is persisted and returned."""
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "AutoComplete Experiment"},
        headers=headers,
    )
    assert resp.status == 201
    experiment_id = (await resp.json())["id"]

    resp = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/runs",
        json={"name": "Run with timeout", "auto_complete_after_minutes": 60},
        headers=headers,
    )
    assert resp.status == 201
    body = await resp.json()
    assert body["auto_complete_after_minutes"] == 60

    # GET also returns the field
    run_id = body["id"]
    resp = await service_client.get(f"/api/v1/runs/{run_id}", headers=headers)
    assert resp.status == 200
    assert (await resp.json())["auto_complete_after_minutes"] == 60


@pytest.mark.asyncio
async def test_update_run_auto_complete_after_minutes(service_client):
    """PATCH can update auto_complete_after_minutes."""
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "AutoComplete Update Exp"},
        headers=headers,
    )
    assert resp.status == 201
    experiment_id = (await resp.json())["id"]

    resp = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/runs",
        json={"name": "Run to patch"},
        headers=headers,
    )
    assert resp.status == 201
    run_id = (await resp.json())["id"]
    assert (await resp.json())["auto_complete_after_minutes"] is None

    resp = await service_client.patch(
        f"/api/v1/runs/{run_id}",
        json={"auto_complete_after_minutes": 30},
        headers=headers,
    )
    assert resp.status == 200
    assert (await resp.json())["auto_complete_after_minutes"] == 30
