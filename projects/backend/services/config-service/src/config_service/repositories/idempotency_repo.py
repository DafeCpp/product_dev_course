"""Idempotency repository for config-service (TTL via expires_at)."""
from __future__ import annotations

import json
from datetime import datetime
from typing import Any
from uuid import UUID

from asyncpg import Record

from backend_common.repositories.base import BaseRepository
from config_service.domain.models import IdempotencyRecord


class IdempotencyRepository(BaseRepository):

    async def get(self, key: str) -> IdempotencyRecord | None:
        row = await self._fetchrow(
            """
            SELECT * FROM idempotency_keys
            WHERE idempotency_key = $1 AND expires_at > NOW()
            """,
            key,
        )
        if row is None:
            return None
        return self._to_record(row)

    async def save(
        self,
        *,
        key: str,
        user_id: str,
        request_path: str,
        request_hash: str,
        response_status: int,
        response_body: dict[str, Any],
        expires_at: datetime,
    ) -> None:
        await self._execute(
            """
            INSERT INTO idempotency_keys (
                idempotency_key, user_id, request_path, request_hash,
                response_status, response_body, expires_at
            ) VALUES ($1, $2, $3, $4, $5, $6::jsonb, $7)
            ON CONFLICT (idempotency_key) DO NOTHING
            """,
            key, user_id, request_path, request_hash,
            response_status,
            json.dumps(response_body, sort_keys=True, separators=(",", ":"), default=str),
            expires_at,
        )

    async def delete_expired(self) -> int:
        result = await self._execute(
            "DELETE FROM idempotency_keys WHERE expires_at <= NOW()"
        )
        return int(result.split()[-1])

    @staticmethod
    def _to_record(row: Record) -> IdempotencyRecord:
        body = row["response_body"]
        if isinstance(body, str):
            body = json.loads(body)
        return IdempotencyRecord(
            id=row["id"],
            idempotency_key=row["idempotency_key"],
            user_id=row["user_id"],
            request_path=row["request_path"],
            request_hash=row["request_hash"],
            response_status=row["response_status"],
            response_body=body,
            expires_at=row["expires_at"],
            created_at=row["created_at"],
        )
