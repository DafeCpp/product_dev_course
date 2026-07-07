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
    response_status: int | None
    response_body: dict | None
    completed: bool


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
                   response_body,
                   completed
            FROM request_idempotency
            WHERE idempotency_key = $1
            """,
            key,
        )
        if record is None:
            return None
        return self._to_record(record)

    async def reserve(
        self,
        key: str,
        user_id: UUID,
        request_path: str,
        request_body_hash: bytes,
    ) -> bool:
        """Insert a pending placeholder to claim this idempotency key.

        Returns ``True`` if the placeholder was inserted (this request owns
        the key and may proceed with the mutation). Returns ``False`` if the
        key already exists (another request is in progress or already
        completed — caller should inspect the existing record).
        """
        record = await self._fetchrow(
            """
            INSERT INTO request_idempotency (
                idempotency_key,
                user_id,
                request_path,
                request_body_hash,
                completed
            )
            VALUES ($1, $2, $3, $4, false)
            ON CONFLICT (idempotency_key) DO NOTHING
            RETURNING idempotency_key
            """,
            key,
            user_id,
            request_path,
            request_body_hash,
        )
        return record is not None

    async def complete(
        self,
        key: str,
        response_status: int,
        response_body: dict,
    ) -> None:
        """Mark a reserved key as complete with the actual response."""
        await self._execute(
            """
            UPDATE request_idempotency
            SET completed = true,
                response_status = $2,
                response_body = $3::jsonb
            WHERE idempotency_key = $1
            """,
            key,
            response_status,
            json.dumps(response_body, sort_keys=True, separators=(",", ":"), default=str),
        )

    async def release(self, key: str) -> None:
        """Remove a reserved-but-incomplete placeholder for *key*.

        Used when the mutation that owns the reservation fails: dropping the
        ``in_progress`` row lets the client retry with the same key instead of
        getting stuck on 503 until TTL cleanup. Never touches a completed
        record (its cached response must survive for replay).
        """
        await self._execute(
            "DELETE FROM request_idempotency WHERE idempotency_key = $1 AND completed = false",
            key,
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
        elif body is None:
            body = None
        return IdempotencyRecord(
            key=record["idempotency_key"],
            user_id=record["user_id"],
            request_path=record["request_path"],
            request_body_hash=record["request_body_hash"],
            response_status=record["response_status"],
            response_body=body,
            completed=record["completed"],
        )
