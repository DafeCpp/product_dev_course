"""Idempotency repository."""
from __future__ import annotations

import json
from dataclasses import dataclass
from datetime import datetime
from uuid import UUID

from asyncpg import Record  # type: ignore[import-untyped]

from experiment_service.repositories.base import BaseRepository


@dataclass
class IdempotencyRecord:
    key: str
    user_id: UUID
    request_path: str
    request_body_hash: bytes
    response_status: int
    response_body: dict


class IdempotencyRepository(BaseRepository):
    """Persistence layer for request idempotency records."""

    async def get(self, key: str) -> IdempotencyRecord | None:
        record = await self._fetchrow(
            """
            SELECT idempotency_key,
                   user_id,
                   request_path,
                   request_body_hash,
                   response_status,
                   response_body
            FROM request_idempotency
            WHERE idempotency_key = $1
            """,
            key,
        )
        if record is None:
            return None
        return self._to_record(record)

    async def save(
        self,
        key: str,
        user_id: UUID,
        request_path: str,
        request_body_hash: bytes,
        response_status: int,
        response_body: dict,
    ) -> None:
        await self._execute(
            """
            INSERT INTO request_idempotency (
                idempotency_key,
                user_id,
                request_path,
                request_body_hash,
                response_status,
                response_body
            )
            VALUES ($1, $2, $3, $4, $5, $6::jsonb)
            ON CONFLICT (idempotency_key) DO NOTHING
            """,
            key,
            user_id,
            request_path,
            request_body_hash,
            response_status,
            json.dumps(response_body, sort_keys=True, separators=(",", ":"), default=str),
        )

    async def delete_expired(self, created_before: datetime) -> int:
        """Delete idempotency records older than *created_before*. Returns count."""
        result = await self._execute(
            "DELETE FROM request_idempotency WHERE created_at < $1",
            created_before,
        )
        return int(result.split()[-1])

    @staticmethod
    def _to_record(record: Record) -> IdempotencyRecord:
        body = record["response_body"]
        if isinstance(body, str):
            body = json.loads(body)
        return IdempotencyRecord(
            key=record["idempotency_key"],
            user_id=record["user_id"],
            request_path=record["request_path"],
            request_body_hash=record["request_body_hash"],
            response_status=record["response_status"],
            response_body=body,
        )



