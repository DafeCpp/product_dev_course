"""Experiment-service adapter for the shared idempotency service."""
from __future__ import annotations

from datetime import timedelta

from backend_common.idempotency import IDEMPOTENCY_HEADER, IdempotencyPayload
from backend_common.idempotency import IdempotencyService as CommonIdempotencyService

from experiment_service.core.exceptions import IdempotencyConflictError
from experiment_service.repositories.idempotency import IdempotencyRepository
from experiment_service.settings import settings


class IdempotencyService(CommonIdempotencyService):
    def __init__(self, repository: IdempotencyRepository) -> None:
        super().__init__(
            repository,
            ttl=timedelta(hours=settings.idempotency_ttl_hours),
            conflict_error_factory=lambda _key, reason: IdempotencyConflictError(reason),
        )


__all__ = ["IDEMPOTENCY_HEADER", "IdempotencyPayload", "IdempotencyService"]
