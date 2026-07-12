"""Shared Idempotency-Key repository and service helpers."""
from __future__ import annotations

import hashlib
import json
from contextlib import asynccontextmanager
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta
from typing import Any, AsyncIterator, Callable

from aiohttp import web
from asyncpg import Record  # type: ignore[import-untyped]

from backend_common.repositories.base import BaseRepository

IDEMPOTENCY_HEADER = "Idempotency-Key"
DEFAULT_TABLE_NAME = "idempotency_keys"


@dataclass(init=False)
class IdempotencyRecord:
    key: str
    user_id: str
    request_path: str
    request_hash: str
    response_status: int | None
    response_body: dict[str, Any] | None
    completed: bool
    expires_at: datetime
    created_at: datetime | None = None

    def __init__(
        self,
        key: str | None = None,
        user_id: str = "",
        request_path: str = "",
        request_hash: str | None = None,
        response_status: int | None = None,
        response_body: dict[str, Any] | None = None,
        completed: bool = False,
        expires_at: datetime | None = None,
        created_at: datetime | None = None,
        idempotency_key: str | None = None,
        request_body_hash: bytes | None = None,
    ) -> None:
        self.key = key or idempotency_key or ""
        self.user_id = str(user_id)
        self.request_path = request_path
        if request_hash is None and request_body_hash is not None:
            request_hash = request_body_hash.hex()
        self.request_hash = request_hash or ""
        self.response_status = response_status
        self.response_body = response_body
        self.completed = completed
        self.expires_at = expires_at or datetime.now(tz=UTC)
        self.created_at = created_at

    @property
    def idempotency_key(self) -> str:
        return self.key

    @property
    def request_body_hash(self) -> bytes:
        return bytes.fromhex(self.request_hash)


@dataclass
class IdempotencyPayload:
    status: int
    body: dict[str, Any]


class IdempotencyConflictError(Exception):
    """Raised when an idempotency key is reused for a different request."""


class IdempotencyRepository(BaseRepository):
    """Persistence layer for the shared ``idempotency_keys`` table.

    The table uses a reservation-first workflow: a request atomically inserts a
    pending row before running the mutation, then stores the final response when
    the mutation succeeds. This prevents concurrent requests with the same key
    from executing the mutation twice.
    """

    def __init__(self, pool: Any, *, table_name: str = DEFAULT_TABLE_NAME) -> None:
        super().__init__(pool)
        self._table_name = table_name

    async def get(self, key: str) -> IdempotencyRecord | None:
        row = await self._fetchrow(
            f"""
            SELECT idempotency_key,
                   user_id,
                   request_path,
                   request_hash,
                   response_status,
                   response_body,
                   completed,
                   expires_at,
                   created_at
            FROM {self._table_name}
            WHERE idempotency_key = $1 AND expires_at > NOW()
            """,
            key,
        )
        if row is None:
            return None
        return self._to_record(row)

    async def reserve(
        self,
        key: str,
        user_id: str,
        request_path: str,
        request_hash: str,
        expires_at: datetime,
    ) -> bool:
        row = await self._fetchrow(
            f"""
            INSERT INTO {self._table_name} (
                idempotency_key, user_id, request_path, request_hash, expires_at, completed
            ) VALUES ($1, $2, $3, $4, $5, false)
            ON CONFLICT (idempotency_key) DO NOTHING
            RETURNING idempotency_key
            """,
            key,
            user_id,
            request_path,
            request_hash,
            expires_at,
        )
        return row is not None

    async def complete(self, key: str, response_status: int, response_body: dict[str, Any]) -> None:
        await self._execute(
            f"""
            UPDATE {self._table_name}
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
        await self._execute(
            f"DELETE FROM {self._table_name} WHERE idempotency_key = $1 AND completed = false",
            key,
        )

    async def delete_expired(self) -> int:
        result = await self._execute(f"DELETE FROM {self._table_name} WHERE expires_at <= NOW()")
        return int(result.split()[-1])

    @staticmethod
    def _to_record(row: Record) -> IdempotencyRecord:
        body = row["response_body"]
        if isinstance(body, str):
            body = json.loads(body)
        return IdempotencyRecord(
            key=row["idempotency_key"],
            user_id=str(row["user_id"]),
            request_path=row["request_path"],
            request_hash=row["request_hash"],
            response_status=row["response_status"],
            response_body=body,
            completed=row["completed"],
            expires_at=row["expires_at"],
            created_at=row["created_at"],
        )


class IdempotencyService:
    """Shared Idempotency-Key workflow for aiohttp handlers."""

    def __init__(
        self,
        repository: IdempotencyRepository,
        *,
        ttl: timedelta,
        conflict_error_factory: Callable[[str], Exception] | None = None,
    ) -> None:
        self._repository = repository
        self._ttl = ttl
        self._conflict_error_factory = conflict_error_factory or IdempotencyConflictError

    @staticmethod
    def body_hash(body: dict[str, Any]) -> str:
        serialized = json.dumps(body, sort_keys=True, separators=(",", ":"), default=str)
        return hashlib.sha256(serialized.encode("utf-8")).hexdigest()

    @staticmethod
    def canonical_body(body: dict[str, Any]) -> tuple[str, str]:
        serialized = json.dumps(body, sort_keys=True, separators=(",", ":"), default=str)
        return serialized, hashlib.sha256(serialized.encode("utf-8")).hexdigest()

    async def reserve_or_get_cached(
        self,
        key: str,
        user_id: str,
        request_path: str,
        body_hash: str,
    ) -> IdempotencyPayload | None:
        expires_at = datetime.now(tz=UTC) + self._ttl
        reserved = await self._repository.reserve(key, str(user_id), request_path, body_hash, expires_at)
        if reserved:
            return None
        existing = await self._repository.get(key)
        if existing is None:
            return None
        self._assert_record(existing, str(user_id), request_path, body_hash)
        if not existing.completed:
            raise web.HTTPServiceUnavailable(
                text="Duplicate request in progress — retry with the same Idempotency-Key after the original completes"
            )
        assert existing.response_status is not None and existing.response_body is not None
        return IdempotencyPayload(existing.response_status, existing.response_body)

    async def complete_response(self, key: str, response_status: int, response_body: dict[str, Any]) -> None:
        await self._repository.complete(key, response_status, response_body)

    async def release(self, key: str) -> None:
        await self._repository.release(key)

    @asynccontextmanager
    async def guard_reservation(self, key: str | None) -> AsyncIterator[None]:
        try:
            yield
        except BaseException:
            if key:
                await self.release(key)
            raise

    @staticmethod
    def build_response(payload: IdempotencyPayload) -> web.Response:
        return web.json_response(payload.body, status=payload.status)

    async def get_cached_response(self, key: str, user_id: str, request_path: str, body_hash: str) -> IdempotencyPayload | None:
        """Compatibility helper for legacy handlers that do not reserve first."""
        record = await self._repository.get(key)
        if record is None:
            return None
        self._assert_record(record, str(user_id), request_path, body_hash)
        if not record.completed:
            raise web.HTTPServiceUnavailable(text="Duplicate request in progress — retry with the same Idempotency-Key after the original completes")
        assert record.response_status is not None and record.response_body is not None
        return IdempotencyPayload(record.response_status, record.response_body)

    async def store_response(self, key: str, user_id: str, request_path: str, body_hash: str, response_status: int, response_body: dict[str, Any]) -> None:
        await self.reserve_or_get_cached(key, str(user_id), request_path, body_hash)
        await self.complete_response(key, response_status, response_body)

    def _assert_record(
        self, record: IdempotencyRecord, user_id: Any, request_path: str, body_hash: Any
    ) -> None:
        normalized_hash = body_hash.hex() if isinstance(body_hash, bytes) else str(body_hash)
        if record.user_id != str(user_id) or record.request_path != request_path:
            raise self._conflict_error_factory(record.key)
        if record.request_hash != normalized_hash:
            raise self._conflict_error_factory(record.key)
