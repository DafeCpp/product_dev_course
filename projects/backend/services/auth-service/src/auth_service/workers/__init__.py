"""Background workers for auth-service."""
from __future__ import annotations

from backend_common.worker import BackgroundWorker, WorkerTask

from auth_service.settings import settings
from auth_service.workers.audit_log_cleanup import audit_log_cleanup
from auth_service.workers.token_cleanup import token_cleanup

worker = BackgroundWorker(
    interval_seconds=settings.worker_interval_seconds,
    tasks=[
        WorkerTask(name="audit_log_cleanup", fn=audit_log_cleanup),
        WorkerTask(name="token_cleanup", fn=token_cleanup),
    ],
)

start_background_worker = worker.start
stop_background_worker = worker.stop

__all__ = [
    "worker",
    "start_background_worker",
    "stop_background_worker",
]
