"""Background workers for experiment-service.

Each worker is a standalone module exporting a single async task function
compatible with :class:`backend_common.worker.WorkerTask`.

The :data:`worker` instance aggregates all tasks and provides
``start_background_worker`` / ``stop_background_worker`` lifecycle hooks.
"""
from __future__ import annotations

from backend_common.worker import BackgroundWorker, WorkerTask

from experiment_service.settings import settings
from experiment_service.workers.activate_scheduled_profiles import activate_scheduled_profiles
from experiment_service.workers.audit_log_cleanup import audit_log_cleanup
from experiment_service.workers.conversion_backfill import conversion_backfill
from experiment_service.workers.idempotency_cleanup import idempotency_cleanup
from experiment_service.workers.scheduled_profile_activation import scheduled_profile_activation
from experiment_service.workers.stale_session_cleanup import stale_session_cleanup
from experiment_service.workers.webhook_purge import webhook_purge_succeeded
from experiment_service.workers.webhook_reclaim import webhook_reclaim_stuck

worker = BackgroundWorker(
    interval_seconds=settings.worker_interval_seconds,
    tasks=[
        WorkerTask(name="idempotency_cleanup", fn=idempotency_cleanup),
        WorkerTask(name="stale_session_cleanup", fn=stale_session_cleanup),
        WorkerTask(name="scheduled_profile_activation", fn=scheduled_profile_activation),
        WorkerTask(name="webhook_reclaim_stuck", fn=webhook_reclaim_stuck),
        WorkerTask(name="webhook_purge_succeeded", fn=webhook_purge_succeeded),
        WorkerTask(name="conversion_backfill", fn=conversion_backfill),
        WorkerTask(name="audit_log_cleanup", fn=audit_log_cleanup),
        WorkerTask(name="activate_scheduled_profiles", fn=activate_scheduled_profiles),
    ],
)

start_background_worker = worker.start
stop_background_worker = worker.stop

__all__ = [
    "worker",
    "start_background_worker",
    "stop_background_worker",
]
