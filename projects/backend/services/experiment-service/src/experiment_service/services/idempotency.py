"""Idempotency service."""
from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from typing import Any, Tuple
from uuid import UUID

from aiohttp import web

from experiment_service.core.exceptions import IdempotencyConflictError
from experiment_service.repositories.idempotency import IdempotencyRecord, IdempotencyRepository

IDEMPOTENCY_HEADER = "Idempotency-Key"


@dataclass
class IdempotencyPayload:
    status: int
    body: dict[str, Any]


class IdempotencyService:
    """High-level helper for handling idempotent requests."""

    def __init__(self, repository: IdempotencyRepository):
        self._repository = repository

    @staticmethod
    def canonical_body(body: dict[str, Any]) -> Tuple[str, bytes]:
        serialized = json.dumps(body, sort_keys=True, separators=(",", ":"), default=str)
        digest = hashlib.sha256(serialized.encode("utf-8")).digest()
        return serialized, digest

    async def reserve_or_get_cached(
        self,
        key: str,
        user_id: UUID,
        request_path: str,
        body_hash: bytes,
    ) -> IdempotencyPayload | None:
        """Reserve this key before executing a mutation, or return a cached result.

        Inserts a pending placeholder row before the mutation executes so that
        concurrent requests with the same key cannot both run the mutation.

        Returns ``None`` when this request has reserved the key and may proceed
        with the mutation. Returns an ``IdempotencyPayload`` when the key was
        already completed by a previous (or concurrent) request — the caller
        should return this payload directly.

        Raises ``IdempotencyConflictError`` (→ 409) if the key was used by a
        different user / path / body. Raises ``HTTPServiceUnavailable`` (→ 503)
        if another request currently holds the key (in progress).
        """
        reserved = await self._repository.reserve(key, user_id, request_path, body_hash)
        if reserved:
            return None  # we own the key — proceed with mutation

        existing = await self._repository.get(key)
        if existing is None:
            # Row was removed between reserve and get (TTL cleanup); treat as a miss.
            return None
        self._assert_record(existing, user_id, request_path, body_hash)
        if not existing.completed:
            raise web.HTTPServiceUnavailable(
                text="Duplicate request in progress — retry with the same Idempotency-Key after the original completes"
            )
        assert existing.response_status is not None and existing.response_body is not None
        return IdempotencyPayload(status=existing.response_status, body=existing.response_body)

    async def complete_response(
        self,
        key: str,
        response_status: int,
        response_body: dict[str, Any],
    ) -> None:
        """Mark the reserved key as complete with the actual response."""
        await self._repository.complete(key, response_status, response_body)

    @staticmethod
    def build_response(payload: IdempotencyPayload) -> web.Response:
        return web.json_response(payload.body, status=payload.status)

    @staticmethod
    def _assert_record(
        record: IdempotencyRecord,
        user_id: UUID,
        request_path: str,
        body_hash: bytes,
    ) -> None:
        if record.user_id != user_id or record.request_path != request_path:
            raise IdempotencyConflictError("Idempotency key belongs to another request")
        if record.request_body_hash != body_hash:
            raise IdempotencyConflictError("Idempotency key reused with different payload")
