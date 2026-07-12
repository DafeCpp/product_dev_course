"""Config-service adapter for the shared idempotency service."""
from __future__ import annotations

from datetime import timedelta

from backend_common.idempotency import IDEMPOTENCY_HEADER, IdempotencyPayload
from backend_common.idempotency import IdempotencyService as CommonIdempotencyService

from config_service.core.exceptions import IdempotencyConflictError
from config_service.repositories.idempotency_repo import IdempotencyRepository
from config_service.settings import settings


class IdempotencyService(CommonIdempotencyService):
    def __init__(self, repository: IdempotencyRepository) -> None:
        super().__init__(
            repository,
            ttl=timedelta(minutes=settings.idempotency_ttl_minutes),
            conflict_error_factory=lambda key, _reason: IdempotencyConflictError(key),
        )


__all__ = ["IDEMPOTENCY_HEADER", "IdempotencyPayload", "IdempotencyService"]
