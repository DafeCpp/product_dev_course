"""Experiment-service adapter for the shared idempotency service."""
from __future__ import annotations

from datetime import timedelta
from typing import Any

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
            conflict_error_factory=IdempotencyConflictError,
        )

    @staticmethod
    def canonical_body(body: dict[str, Any]) -> tuple[str, bytes]:  # compatibility
        serialized, hexdigest = CommonIdempotencyService.canonical_body(body)
        return serialized, bytes.fromhex(hexdigest)

    @staticmethod
    def body_hash(body: dict[str, Any]) -> str:
        return CommonIdempotencyService.body_hash(body)

    async def reserve_or_get_cached(self, key: str, user_id: Any, request_path: str, body_hash: Any):
        if isinstance(body_hash, bytes):
            body_hash = body_hash.hex()
        return await super().reserve_or_get_cached(key, str(user_id), request_path, body_hash)


__all__ = ["IDEMPOTENCY_HEADER", "IdempotencyPayload", "IdempotencyService"]
