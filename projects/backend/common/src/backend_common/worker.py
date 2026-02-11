"""Reusable periodic background worker for aiohttp services.

Usage::

    from backend_common.worker import BackgroundWorker, WorkerTask

    async def cleanup_old_tokens(now: datetime) -> str | None:
        deleted = await repo.delete_expired(now - timedelta(hours=48))
        return f"deleted={deleted}" if deleted else None

    worker = BackgroundWorker(
        interval_seconds=60.0,
        tasks=[
            WorkerTask(name="token_cleanup", fn=cleanup_old_tokens),
        ],
    )

    # In create_app():
    app.on_startup.append(worker.start)
    app.on_cleanup.append(worker.stop)
"""
from __future__ import annotations

import asyncio
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Awaitable, Callable, Sequence

import structlog
from aiohttp import web

logger = structlog.get_logger(__name__)

# Type for a single task function: receives current UTC time, returns
# an optional human-readable summary string (logged when non-empty).
TaskFn = Callable[[datetime], Awaitable[str | None]]


@dataclass
class WorkerTask:
    """A named periodic task executed by :class:`BackgroundWorker`."""

    name: str
    fn: TaskFn


_WORKER_TASK_KEY = "__background_worker_task__"


@dataclass
class BackgroundWorker:
    """In-process async worker that runs a list of tasks in a loop.

    Each task is executed independently — if one fails the others still run.
    The worker is resilient to transient errors and logs them via structlog.

    Lifecycle is managed through :meth:`start` / :meth:`stop` which are
    compatible with ``app.on_startup`` / ``app.on_cleanup``.
    """

    interval_seconds: float = 60.0
    tasks: Sequence[WorkerTask] = field(default_factory=list)

    async def start(self, app: web.Application) -> None:
        """Create the worker asyncio task. Register with ``app.on_startup``."""
        app[_WORKER_TASK_KEY] = asyncio.create_task(self._loop(app))

    async def stop(self, app: web.Application) -> None:
        """Cancel the worker task. Register with ``app.on_cleanup``."""
        task = app.get(_WORKER_TASK_KEY)
        if task is not None:
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass

    async def _loop(self, _app: web.Application) -> None:
        task_names = [t.name for t in self.tasks]
        logger.info(
            "background_worker started",
            interval_seconds=self.interval_seconds,
            tasks=task_names,
        )

        while True:
            try:
                await asyncio.sleep(self.interval_seconds)
                now = datetime.now(timezone.utc)

                for task in self.tasks:
                    try:
                        summary = await task.fn(now)
                        if summary:
                            logger.info(
                                "background_task completed",
                                task=task.name,
                                summary=summary,
                            )
                    except Exception:
                        logger.exception(
                            "background_task failed",
                            task=task.name,
                        )

            except asyncio.CancelledError:
                logger.info("background_worker stopped")
                raise
            except Exception:
                logger.exception("background_worker sweep failed")
