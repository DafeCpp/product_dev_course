"""Idempotency service for config-service."""
from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from typing import Any

from aiohttp import web

from config_service.core.exceptions import IdempotencyConflictError
from config_service.domain.models import IdempotencyRecord
from config_service.repositories.idempotency_repo import IdempotencyRepository
from config_service.settings import settings

IDEMPOTENCY_HEADER = "Idempotency-Key"


@dataclass
class IdempotencyPayload:
    status: int
    body: dict[str, Any]


class IdempotencyService:
    def __init__(self, repository: IdempotencyRepository) -> None:
        self._repository = repository

    @staticmethod
    def body_hash(body: dict[str, Any]) -> str:
        serialized = json.dumps(body, sort_keys=True, separators=(",", ":"), default=str)
        return hashlib.sha256(serialized.encode()).hexdigest()

    async def get_cached_response(
        self,
        key: str,
        user_id: str,
        request_path: str,
        body_hash: str,
    ) -> IdempotencyPayload | None:
        record = await self._repository.get(key)
        if record is None:
            return None
        # Different user — treat as independent (no conflict, no cache)
        if record.user_id != user_id:
            return None
        self._assert_record(record, user_id, request_path, body_hash)
        return IdempotencyPayload(status=record.response_status, body=record.response_body)

    async def store_response(
        self,
        key: str,
        user_id: str,
        request_path: str,
        body_hash: str,
        response_status: int,
        response_body: dict[str, Any],
    ) -> None:
        expires_at = datetime.now(tz=timezone.utc) + timedelta(
            minutes=settings.idempotency_ttl_minutes
        )
        await self._repository.save(
            key=key,
            user_id=user_id,
            request_path=request_path,
            request_hash=body_hash,
            response_status=response_status,
            response_body=response_body,
            expires_at=expires_at,
        )

    @staticmethod
    def build_response(payload: IdempotencyPayload) -> web.Response:
        return web.json_response(payload.body, status=payload.status)

    @staticmethod
    def _assert_record(
        record: IdempotencyRecord,
        user_id: str,
        request_path: str,
        body_hash: str,
    ) -> None:
        if record.user_id != user_id or record.request_path != request_path:
            raise IdempotencyConflictError(record.idempotency_key)
        if record.request_hash != body_hash:
            raise IdempotencyConflictError(record.idempotency_key)
