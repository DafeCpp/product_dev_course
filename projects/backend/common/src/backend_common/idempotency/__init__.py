"""Shared Idempotency-Key repository and service helpers.

Keys are scoped to ``(idempotency_key, user_id)``: the same key sent by two
different users describes two independent requests, not a conflict.

Reserving a key hands back a :class:`Reservation` — the handle a caller uses to
complete or release it. The handle carries a generation token, which fences the
completion against a specific row: if this request outlives its TTL and a retry
reclaims the expired row, the token no longer matches and the stale owner cannot
overwrite the retry's reservation with its own response.
"""
from __future__ import annotations

import hashlib
import json
import uuid
from contextlib import asynccontextmanager
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta
from typing import Any, AsyncIterator, Callable

from aiohttp import web
from asyncpg import Record  # type: ignore[import-untyped]

from backend_common.repositories.base import BaseRepository

IDEMPOTENCY_HEADER = "Idempotency-Key"
DEFAULT_TABLE_NAME = "idempotency_keys"

CONFLICT_OTHER_REQUEST = "Idempotency key belongs to another request"
CONFLICT_DIFFERENT_PAYLOAD = "Idempotency key reused with different payload"

_IN_PROGRESS_TEXT = (
    "Duplicate request in progress — retry with the same Idempotency-Key "
    "after the original completes"
)

# How many times reserve() may lose the insert and then find nothing to read back
# before we give up and make the client retry. See reserve_or_get_cached().
_RESERVE_ATTEMPTS = 3


@dataclass(frozen=True)
class Reservation:
    """A held idempotency key: what the owner passes back to complete or release it."""

    key: str
    user_id: str
    token: uuid.UUID


@dataclass
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


@dataclass
class IdempotencyPayload:
    status: int
    body: dict[str, Any]


class IdempotencyConflictError(Exception):
    """Raised when an idempotency key is reused for a different request."""


def _dumps(body: dict[str, Any]) -> str:
    return json.dumps(body, sort_keys=True, separators=(",", ":"), default=str)


class IdempotencyRepository(BaseRepository):
    """Persistence layer for the shared idempotency table.

    The table uses a reservation-first workflow: a request atomically inserts a
    pending row before running the mutation, then stores the final response when
    the mutation succeeds. This prevents concurrent requests with the same key
    from executing the mutation twice.

    ``table_name`` exists because services name their table differently
    (``idempotency_keys`` in config-service, ``request_idempotency`` in
    experiment-service); the column layout is identical.
    """

    def __init__(self, pool: Any, *, table_name: str = DEFAULT_TABLE_NAME) -> None:
        super().__init__(pool)
        self._table_name = table_name

    async def get(self, key: str, user_id: str) -> IdempotencyRecord | None:
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
            WHERE idempotency_key = $1 AND user_id = $2 AND expires_at > NOW()
            """,
            key,
            user_id,
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
        token: uuid.UUID,
    ) -> bool:
        """Insert a pending row, returning True when this caller owns the key.

        An expired row is taken over in the same statement: without that, the
        insert would lose the uniqueness race against a row that ``get`` then
        filters out by ``expires_at``, and the caller would run the mutation
        with no reservation protecting it. The takeover installs a fresh token,
        which is what invalidates the previous owner's completion.
        """
        row = await self._fetchrow(
            f"""
            INSERT INTO {self._table_name} (
                idempotency_key, user_id, request_path, request_hash,
                expires_at, completed, reservation_token
            ) VALUES ($1, $2, $3, $4, $5, false, $6)
            ON CONFLICT (idempotency_key, user_id) DO UPDATE
            SET request_path      = EXCLUDED.request_path,
                request_hash      = EXCLUDED.request_hash,
                expires_at        = EXCLUDED.expires_at,
                reservation_token = EXCLUDED.reservation_token,
                completed         = false,
                response_status   = NULL,
                response_body     = NULL,
                created_at        = NOW()
            WHERE {self._table_name}.expires_at <= NOW()
            RETURNING idempotency_key
            """,
            key,
            user_id,
            request_path,
            request_hash,
            expires_at,
            token,
        )
        return row is not None

    async def complete(
        self,
        key: str,
        user_id: str,
        token: uuid.UUID,
        response_status: int,
        response_body: dict[str, Any],
    ) -> bool:
        """Store the response against the reservation identified by ``token``.

        Returns False when the token no longer matches — the row was reclaimed
        by a retry after this request outlived its TTL, so this response is
        stale and must not be cached against the new owner's reservation.
        """
        result = await self._execute(
            f"""
            UPDATE {self._table_name}
            SET completed = true,
                response_status = $4,
                response_body = $5::jsonb
            WHERE idempotency_key = $1 AND user_id = $2 AND reservation_token = $3
            """,
            key,
            user_id,
            token,
            response_status,
            _dumps(response_body),
        )
        return result.split()[-1] != "0"

    async def release(self, key: str, user_id: str, token: uuid.UUID) -> None:
        await self._execute(
            f"""
            DELETE FROM {self._table_name}
            WHERE idempotency_key = $1
              AND user_id = $2
              AND reservation_token = $3
              AND completed = false
            """,
            key,
            user_id,
            token,
        )

    async def delete_expired(self) -> int:
        result = await self._execute(
            f"DELETE FROM {self._table_name} WHERE expires_at <= NOW()"
        )
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
    """Shared Idempotency-Key workflow for aiohttp handlers.

    ``conflict_error_factory`` receives ``(key, reason)`` so each service can
    raise its own 409 exception type while keeping the shared reason strings.
    """

    def __init__(
        self,
        repository: IdempotencyRepository,
        *,
        ttl: timedelta,
        conflict_error_factory: Callable[[str, str], Exception] | None = None,
    ) -> None:
        self._repository = repository
        self._ttl = ttl
        self._conflict_error_factory = conflict_error_factory or (
            lambda key, reason: IdempotencyConflictError(reason)
        )

    @staticmethod
    def body_hash(body: dict[str, Any]) -> str:
        return hashlib.sha256(_dumps(body).encode("utf-8")).hexdigest()

    @staticmethod
    def canonical_body(body: dict[str, Any]) -> tuple[str, str]:
        serialized = _dumps(body)
        return serialized, hashlib.sha256(serialized.encode("utf-8")).hexdigest()

    async def reserve_or_get_cached(
        self,
        key: str,
        user_id: Any,
        request_path: str,
        body_hash: str,
    ) -> tuple[Reservation | None, IdempotencyPayload | None]:
        """Reserve the key before a mutation, or return the cached result.

        Returns ``(reservation, None)`` when this caller owns the key and may run
        the mutation, and ``(None, payload)`` when a previous request already
        completed it. Raises a 409 on same-key/different-request and a 503 while
        another request holds the key.
        """
        user = str(user_id)
        for _ in range(_RESERVE_ATTEMPTS):
            expires_at = datetime.now(tz=UTC) + self._ttl
            token = uuid.uuid4()
            if await self._repository.reserve(
                key, user, request_path, body_hash, expires_at, token
            ):
                return Reservation(key=key, user_id=user, token=token), None

            existing = await self._repository.get(key, user)
            if existing is None:
                # The row that beat us to the insert expired (or the cleanup worker
                # deleted it) before we could read it back. Returning early here would
                # run the mutation with no reservation guarding it, so try again.
                continue

            self._assert_record(existing, request_path, body_hash)
            if not existing.completed:
                raise web.HTTPServiceUnavailable(text=_IN_PROGRESS_TEXT)
            assert existing.response_status is not None and existing.response_body is not None
            return None, IdempotencyPayload(existing.response_status, existing.response_body)

        # Every attempt lost the insert and then found nothing to read back: the row
        # keeps vanishing under us. Make the client retry rather than run unguarded.
        raise web.HTTPServiceUnavailable(text=_IN_PROGRESS_TEXT)

    async def complete_response(
        self, reservation: Reservation, response_status: int, response_body: dict[str, Any]
    ) -> bool:
        """Cache the response against ``reservation``.

        Returns False when the reservation was reclaimed by a retry while the
        mutation was running (this request outlived its TTL). The response is
        still returned to this caller — it just isn't cached for replay, because
        the row now belongs to somebody else.
        """
        return await self._repository.complete(
            reservation.key,
            reservation.user_id,
            reservation.token,
            response_status,
            response_body,
        )

    async def release(self, reservation: Reservation) -> None:
        await self._repository.release(
            reservation.key, reservation.user_id, reservation.token
        )

    @asynccontextmanager
    async def guard_reservation(self, reservation: Reservation | None) -> AsyncIterator[None]:
        """Release the reservation if the wrapped mutation raises.

        Without this, a failed mutation leaves the key pending and poisons every
        retry with a 503 until the TTL expires. A no-op when ``reservation`` is
        ``None`` (request sent without an Idempotency-Key).
        """
        try:
            yield
        except BaseException:
            if reservation is not None:
                await self.release(reservation)
            raise

    @staticmethod
    def build_response(payload: IdempotencyPayload) -> web.Response:
        return web.json_response(payload.body, status=payload.status)

    def _assert_record(
        self, record: IdempotencyRecord, request_path: str, body_hash: str
    ) -> None:
        # user_id needs no check here: records are fetched scoped to the caller.
        if record.request_path != request_path:
            raise self._conflict_error_factory(record.key, CONFLICT_OTHER_REQUEST)
        if record.request_hash != body_hash:
            raise self._conflict_error_factory(record.key, CONFLICT_DIFFERENT_PAYLOAD)


__all__ = [
    "CONFLICT_DIFFERENT_PAYLOAD",
    "CONFLICT_OTHER_REQUEST",
    "DEFAULT_TABLE_NAME",
    "IDEMPOTENCY_HEADER",
    "IdempotencyConflictError",
    "IdempotencyPayload",
    "IdempotencyRecord",
    "IdempotencyRepository",
    "IdempotencyService",
    "Reservation",
]
