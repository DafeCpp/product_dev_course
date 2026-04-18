"""Unit tests for backend_common.worker module."""
from __future__ import annotations

import asyncio
from datetime import datetime, timezone
from unittest.mock import AsyncMock, patch

import pytest
from aiohttp import web

from backend_common.worker import BackgroundWorker, WorkerTask


class TestWorkerTask:
    """Tests for WorkerTask dataclass."""

    def test_worker_task_creation(self):
        """Test creating a WorkerTask instance."""
        async def dummy_fn(now: datetime) -> str | None:
            return "done"

        task = WorkerTask(name="test_task", fn=dummy_fn)
        assert task.name == "test_task"
        assert task.fn is dummy_fn

    @pytest.mark.asyncio
    async def test_worker_task_execution(self):
        """Test executing a WorkerTask function."""
        async def task_fn(now: datetime) -> str | None:
            assert isinstance(now, datetime)
            return "result"

        task = WorkerTask(name="test", fn=task_fn)
        result = await task.fn(datetime.now(timezone.utc))
        assert result == "result"

    @pytest.mark.asyncio
    async def test_worker_task_returns_none(self):
        """Test task that returns None (no summary)."""
        async def task_fn(now: datetime) -> str | None:
            return None

        task = WorkerTask(name="test", fn=task_fn)
        result = await task_fn(datetime.now(timezone.utc))
        assert result is None


class TestBackgroundWorkerCreation:
    """Tests for BackgroundWorker initialization."""

    def test_default_values(self):
        """Test default worker configuration."""
        worker = BackgroundWorker()
        assert worker.interval_seconds == 60.0
        assert worker.tasks == []

    def test_custom_interval(self):
        """Test worker with custom interval."""
        worker = BackgroundWorker(interval_seconds=30.0)
        assert worker.interval_seconds == 30.0

    def test_with_tasks(self):
        """Test worker with tasks."""
        async def task1(now: datetime) -> str | None:
            return None

        async def task2(now: datetime) -> str | None:
            return None

        worker = BackgroundWorker(
            interval_seconds=10.0,
            tasks=[
                WorkerTask(name="task1", fn=task1),
                WorkerTask(name="task2", fn=task2),
            ],
        )
        assert len(worker.tasks) == 2
        assert worker.tasks[0].name == "task1"
        assert worker.tasks[1].name == "task2"


class TestBackgroundWorkerStartStop:
    """Tests for worker lifecycle management."""

    @pytest.mark.asyncio
    async def test_start_creates_task(self):
        """Test that start() creates an asyncio task."""
        worker = BackgroundWorker(interval_seconds=0.1)
        app = web.Application()

        await worker.start(app)
        assert "_background_worker_task__" in app
        task = app["_background_worker_task__"]
        assert isinstance(task, asyncio.Task)
        assert not task.done()

        # Cleanup
        await worker.stop(app)

    @pytest.mark.asyncio
    async def test_stop_cancels_task(self):
        """Test that stop() cancels the worker task."""
        worker = BackgroundWorker(interval_seconds=1.0)
        app = web.Application()

        await worker.start(app)
        task = app["_background_worker_task__"]
        assert not task.done()

        await worker.stop(app)
        assert task.cancelled() or task.done()

    @pytest.mark.asyncio
    async def test_stop_without_start(self):
        """Test stopping a worker that was never started."""
        worker = BackgroundWorker()
        app = web.Application()

        # Should not raise
        await worker.stop(app)

    @pytest.mark.asyncio
    async def test_start_stop_multiple_times(self):
        """Test starting and stopping multiple times."""
        worker = BackgroundWorker(interval_seconds=0.1)
        app = web.Application()

        for _ in range(3):
            await worker.start(app)
            await asyncio.sleep(0.05)
            await worker.stop(app)


class TestBackgroundWorkerExecution:
    """Tests for worker task execution."""

    @pytest.mark.asyncio
    async def test_successful_task_execution(self):
        """Test that tasks are executed successfully."""
        executed = []

        async def task_fn(now: datetime) -> str | None:
            executed.append(now)
            return "completed"

        worker = BackgroundWorker(
            interval_seconds=0.05,
            tasks=[WorkerTask(name="test", fn=task_fn)],
        )
        app = web.Application()

        await worker.start(app)
        await asyncio.sleep(0.12)  # Allow at least 2 executions
        await worker.stop(app)

        assert len(executed) >= 2
        assert all(isinstance(dt, datetime) for dt in executed)

    @pytest.mark.asyncio
    async def test_task_returning_none(self):
        """Test tasks that return None don't cause issues."""
        async def task_fn(now: datetime) -> str | None:
            return None

        worker = BackgroundWorker(
            interval_seconds=0.05,
            tasks=[WorkerTask(name="test", fn=task_fn)],
        )
        app = web.Application()

        await worker.start(app)
        await asyncio.sleep(0.12)
        await worker.stop(app)
        # Should complete without errors

    @pytest.mark.asyncio
    async def test_task_exception_handling(self):
        """Test that task exceptions are logged but don't crash worker."""
        call_count = [0]

        async def failing_task(now: datetime) -> str | None:
            call_count[0] += 1
            if call_count[0] == 1:
                raise ValueError("Test error")
            return "recovered"

        worker = BackgroundWorker(
            interval_seconds=0.05,
            tasks=[WorkerTask(name="failing", fn=failing_task)],
        )
        app = web.Application()

        with patch("backend_common.worker.logger") as mock_logger:
            await worker.start(app)
            await asyncio.sleep(0.15)
            await worker.stop(app)

            # Should have logged the exception
            assert mock_logger.exception.called
            # Worker should have recovered and executed again
            assert call_count[0] >= 2

    @pytest.mark.asyncio
    async def test_multiple_tasks_execution(self):
        """Test that multiple tasks are executed."""
        executed = {"task1": 0, "task2": 0}

        async def task1(now: datetime) -> str | None:
            executed["task1"] += 1
            return "task1 done"

        async def task2(now: datetime) -> str | None:
            executed["task2"] += 1
            return "task2 done"

        worker = BackgroundWorker(
            interval_seconds=0.05,
            tasks=[
                WorkerTask(name="task1", fn=task1),
                WorkerTask(name="task2", fn=task2),
            ],
        )
        app = web.Application()

        await worker.start(app)
        await asyncio.sleep(0.12)
        await worker.stop(app)

        assert executed["task1"] >= 1
        assert executed["task2"] >= 1

    @pytest.mark.asyncio
    async def test_task_failure_doesnt_stop_others(self):
        """Test that one failing task doesn't stop others."""
        task1_count = [0]
        task2_count = [0]

        async def failing_task(now: datetime) -> str | None:
            task1_count[0] += 1
            raise ValueError("Always fails")

        async def successful_task(now: datetime) -> str | None:
            task2_count[0] += 1
            return "success"

        worker = BackgroundWorker(
            interval_seconds=0.05,
            tasks=[
                WorkerTask(name="failing", fn=failing_task),
                WorkerTask(name="success", fn=successful_task),
            ],
        )
        app = web.Application()

        await worker.start(app)
        await asyncio.sleep(0.12)
        await worker.stop(app)

        # Both tasks should have been attempted
        assert task1_count[0] >= 1
        assert task2_count[0] >= 1


class TestBackgroundWorkerLogging:
    """Tests for worker logging behavior."""

    @pytest.mark.asyncio
    async def test_start_logging(self):
        """Test that worker start is logged."""
        worker = BackgroundWorker(
            interval_seconds=1.0,
            tasks=[WorkerTask(name="test", fn=lambda now: None)],
        )
        app = web.Application()

        with patch("backend_common.worker.logger") as mock_logger:
            await worker.start(app)
            mock_logger.info.assert_called()
            call_args = mock_logger.info.call_args
            assert call_args is not None
            assert call_args[0][0] == "background_worker started"
            assert call_args[1]["interval_seconds"] == 1.0
            assert call_args[1]["tasks"] == ["test"]

            await worker.stop(app)

    @pytest.mark.asyncio
    async def test_task_completion_logging(self):
        """Test that task completion is logged."""
        async def task_fn(now: datetime) -> str | None:
            return "summary"

        worker = BackgroundWorker(
            interval_seconds=0.05,
            tasks=[WorkerTask(name="test", fn=task_fn)],
        )
        app = web.Application()

        with patch("backend_common.worker.logger") as mock_logger:
            await worker.start(app)
            await asyncio.sleep(0.12)
            await worker.stop(app)

            # Should log task completion
            mock_logger.info.assert_called()
            calls = [str(call) for call in mock_logger.info.call_args_list]
            assert any("background_task completed" in str(call) for call in calls)

    @pytest.mark.asyncio
    async def test_task_failure_logging(self):
        """Test that task failures are logged."""
        async def failing_task(now: datetime) -> str | None:
            raise ValueError("Test failure")

        worker = BackgroundWorker(
            interval_seconds=0.05,
            tasks=[WorkerTask(name="failing", fn=failing_task)],
        )
        app = web.Application()

        with patch("backend_common.worker.logger") as mock_logger:
            await worker.start(app)
            await asyncio.sleep(0.1)
            await worker.stop(app)

            mock_logger.exception.assert_called()
            call_args = mock_logger.exception.call_args
            assert call_args is not None
            assert call_args[0][0] == "background_task failed"
            assert call_args[1]["task"] == "failing"


class TestBackgroundWorkerInterval:
    """Tests for worker interval timing."""

    @pytest.mark.asyncio
    async def test_short_interval(self):
        """Test worker with very short interval."""
        count = [0]

        async def task_fn(now: datetime) -> str | None:
            count[0] += 1
            return None

        worker = BackgroundWorker(
            interval_seconds=0.02,
            tasks=[WorkerTask(name="fast", fn=task_fn)],
        )
        app = web.Application()

        await worker.start(app)
        await asyncio.sleep(0.1)
        await worker.stop(app)

        # Should have executed multiple times
        assert count[0] >= 3

    @pytest.mark.asyncio
    async def test_long_interval(self):
        """Test worker with long interval (doesn't execute during test)."""
        async def task_fn(now: datetime) -> str | None:
            return None

        worker = BackgroundWorker(
            interval_seconds=10.0,
            tasks=[WorkerTask(name="slow", fn=task_fn)],
        )
        app = web.Application()

        await worker.start(app)
        await asyncio.sleep(0.1)
        await worker.stop(app)
        # Should complete without executing the task


class TestBackgroundWorkerCancellation:
    """Tests for worker cancellation behavior."""

    @pytest.mark.asyncio
    async def test_cancel_during_task_execution(self):
        """Test cancellation during task execution."""
        async def slow_task(now: datetime) -> str | None:
            await asyncio.sleep(0.5)
            return "done"

        worker = BackgroundWorker(
            interval_seconds=0.05,
            tasks=[WorkerTask(name="slow", fn=slow_task)],
        )
        app = web.Application()

        await worker.start(app)
        await asyncio.sleep(0.02)
        await worker.stop(app)
        # Should complete without hanging

    @pytest.mark.asyncio
    async def test_cancelled_error_handling(self):
        """Test that CancelledError is properly handled."""
        worker = BackgroundWorker(interval_seconds=0.1)
        app = web.Application()

        await worker.start(app)
        task = app["_background_worker_task__"]

        # Manually cancel the task
        task.cancel()

        # Wait for it to complete
        await asyncio.sleep(0.15)

        # Should have raised CancelledError
        assert task.cancelled() or task.done()

        await worker.stop(app)


class TestBackgroundWorkerIntegration:
    """Integration tests for worker with aiohttp application."""

    @pytest.mark.asyncio
    async def test_aiohttp_app_lifecycle(self):
        """Test worker integration with aiohttp app lifecycle."""
        executed = []

        async def task_fn(now: datetime) -> str | None:
            executed.append(now)
            return "executed"

        worker = BackgroundWorker(
            interval_seconds=0.05,
            tasks=[WorkerTask(name="test", fn=task_fn)],
        )

        app = web.Application()
        app.on_startup.append(worker.start)
        app.on_cleanup.append(worker.stop)

        # Simulate app startup
        for handler in app.on_startup:
            await handler(app)

        await asyncio.sleep(0.12)

        # Simulate app cleanup
        for handler in app.on_cleanup:
            await handler(app)

        assert len(executed) >= 1

    @pytest.mark.asyncio
    async def test_multiple_workers_same_app(self):
        """Test running multiple workers in same app."""
        worker1_executed = []
        worker2_executed = []

        async def task1(now: datetime) -> str | None:
            worker1_executed.append(now)
            return None

        async def task2(now: datetime) -> str | None:
            worker2_executed.append(now)
            return None

        worker1 = BackgroundWorker(
            interval_seconds=0.05,
            tasks=[WorkerTask(name="w1", fn=task1)],
        )
        worker2 = BackgroundWorker(
            interval_seconds=0.05,
            tasks=[WorkerTask(name="w2", fn=task2)],
        )

        app = web.Application()

        await worker1.start(app)
        await worker2.start(app)
        await asyncio.sleep(0.12)
        await worker1.stop(app)
        await worker2.stop(app)

        assert len(worker1_executed) >= 1
        assert len(worker2_executed) >= 1
