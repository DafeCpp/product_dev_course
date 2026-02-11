"""Background workers for experiment-service.

Each worker is a standalone module exporting a single async task function
compatible with :class:`backend_common.worker.WorkerTask`.

The :data:`worker` instance aggregates all tasks and provides
``start_background_worker`` / ``stop_background_worker`` lifecycle hooks.
"""
from __future__ import annotations

from backend_common.worker import BackgroundWorker, WorkerTask

from experiment_service.settings import settings
from experiment_service.workers.idempotency_cleanup import idempotency_cleanup
from experiment_service.workers.stale_session_cleanup import stale_session_cleanup
from experiment_service.workers.webhook_purge import webhook_purge_succeeded
from experiment_service.workers.webhook_reclaim import webhook_reclaim_stuck

worker = BackgroundWorker(
    interval_seconds=settings.worker_interval_seconds,
    tasks=[
        WorkerTask(name="idempotency_cleanup", fn=idempotency_cleanup),
        WorkerTask(name="stale_session_cleanup", fn=stale_session_cleanup),
        WorkerTask(name="webhook_reclaim_stuck", fn=webhook_reclaim_stuck),
        WorkerTask(name="webhook_purge_succeeded", fn=webhook_purge_succeeded),
    ],
)

start_background_worker = worker.start
stop_background_worker = worker.stop

__all__ = [
    "worker",
    "start_background_worker",
    "stop_background_worker",
]
