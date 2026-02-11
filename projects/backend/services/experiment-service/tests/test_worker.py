"""Unit tests for backend_common.worker.BackgroundWorker.

These are pure async tests — no database or aiohttp test server required.
"""
from __future__ import annotations

import asyncio
from datetime import datetime, timezone
from unittest.mock import AsyncMock

import pytest
from aiohttp import web

from backend_common.worker import BackgroundWorker, WorkerTask


@pytest.mark.asyncio
async def test_worker_runs_tasks():
    """Worker should call each task function with a datetime argument."""
    called_with: list[datetime] = []

    async def task_fn(now: datetime) -> str | None:
        called_with.append(now)
        return "ok"

    worker = BackgroundWorker(
        interval_seconds=0.05,
        tasks=[WorkerTask(name="test_task", fn=task_fn)],
    )

    app = web.Application()
    await worker.start(app)

    # Let it run a couple of sweeps
    await asyncio.sleep(0.2)
    await worker.stop(app)

    assert len(called_with) >= 2
    for dt in called_with:
        assert isinstance(dt, datetime)
        assert dt.tzinfo is not None  # should be UTC-aware


@pytest.mark.asyncio
async def test_worker_runs_multiple_tasks():
    """All registered tasks should be called each sweep."""
    task_a_count = 0
    task_b_count = 0

    async def task_a(now: datetime) -> str | None:
        nonlocal task_a_count
        task_a_count += 1
        return None

    async def task_b(now: datetime) -> str | None:
        nonlocal task_b_count
        task_b_count += 1
        return None

    worker = BackgroundWorker(
        interval_seconds=0.05,
        tasks=[
            WorkerTask(name="a", fn=task_a),
            WorkerTask(name="b", fn=task_b),
        ],
    )

    app = web.Application()
    await worker.start(app)
    await asyncio.sleep(0.15)
    await worker.stop(app)

    assert task_a_count >= 1
    assert task_b_count >= 1
    # Both should have run the same number of times
    assert task_a_count == task_b_count


@pytest.mark.asyncio
async def test_worker_task_failure_does_not_stop_others():
    """If one task raises, the other tasks should still execute."""
    good_count = 0

    async def bad_task(now: datetime) -> str | None:
        raise RuntimeError("boom")

    async def good_task(now: datetime) -> str | None:
        nonlocal good_count
        good_count += 1
        return None

    worker = BackgroundWorker(
        interval_seconds=0.05,
        tasks=[
            WorkerTask(name="bad", fn=bad_task),
            WorkerTask(name="good", fn=good_task),
        ],
    )

    app = web.Application()
    await worker.start(app)
    await asyncio.sleep(0.2)
    await worker.stop(app)

    assert good_count >= 2, "good_task should keep running despite bad_task failures"


@pytest.mark.asyncio
async def test_worker_stop_is_clean():
    """Stopping the worker should cancel cleanly without raising."""
    call_count = 0

    async def task_fn(now: datetime) -> str | None:
        nonlocal call_count
        call_count += 1
        return None

    worker = BackgroundWorker(
        interval_seconds=0.05,
        tasks=[WorkerTask(name="t", fn=task_fn)],
    )

    app = web.Application()
    await worker.start(app)
    await asyncio.sleep(0.12)

    # Should not raise
    await worker.stop(app)

    count_at_stop = call_count
    await asyncio.sleep(0.1)
    # No more calls after stop
    assert call_count == count_at_stop


@pytest.mark.asyncio
async def test_worker_no_tasks():
    """Worker with empty task list should start and stop cleanly."""
    worker = BackgroundWorker(interval_seconds=0.05, tasks=[])

    app = web.Application()
    await worker.start(app)
    await asyncio.sleep(0.12)
    await worker.stop(app)  # Should not raise


@pytest.mark.asyncio
async def test_worker_stop_without_start():
    """Calling stop without start should be a no-op."""
    worker = BackgroundWorker(interval_seconds=1.0, tasks=[])
    app = web.Application()
    await worker.stop(app)  # Should not raise


@pytest.mark.asyncio
async def test_worker_task_return_none_is_silent():
    """Tasks returning None should not trigger summary logging (just no crash)."""
    fn = AsyncMock(return_value=None)
    worker = BackgroundWorker(
        interval_seconds=0.05,
        tasks=[WorkerTask(name="silent", fn=fn)],
    )

    app = web.Application()
    await worker.start(app)
    await asyncio.sleep(0.12)
    await worker.stop(app)

    assert fn.call_count >= 1
