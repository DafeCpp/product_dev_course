from __future__ import annotations

import uuid
from dataclasses import dataclass, replace
from datetime import UTC, datetime, timedelta
from unittest.mock import AsyncMock, MagicMock

import pytest
from aiohttp import web

from backend_common.idempotency import (
    IdempotencyConflictError,
    IdempotencyRepository,
    IdempotencyRecord,
    IdempotencyService,
)


@pytest.mark.asyncio
async def test_repository_maps_rows_and_reports_write_outcomes() -> None:
    repository = IdempotencyRepository(MagicMock(), table_name="request_idempotency")
    now = datetime.now(tz=UTC)
    row = {
        "idempotency_key": "key-1",
        "user_id": uuid.uuid4(),
        "request_path": "/resource",
        "request_hash": "hash",
        "response_status": 201,
        "response_body": '{"id":"created"}',
        "completed": True,
        "expires_at": now,
        "created_at": now,
    }
    repository._fetchrow = AsyncMock(return_value=row)
    record = await repository.get("key-1", "user-1")
    assert record is not None
    assert record.response_body == {"id": "created"}
    assert record.user_id == str(row["user_id"])

    repository._fetchrow.return_value = None
    assert await repository.get("missing", "user-1") is None

    repository._fetchrow.return_value = {"idempotency_key": "key-1"}
    assert await repository.reserve("key-1", "user-1", "/resource", "hash", now, uuid.uuid4()) is True
    repository._fetchrow.return_value = None
    assert await repository.reserve("key-1", "user-1", "/resource", "hash", now, uuid.uuid4()) is False

    repository._execute = AsyncMock(side_effect=["UPDATE 1", "UPDATE 0", "DELETE 3", "DELETE 2"])
    token = uuid.uuid4()
    assert await repository.complete("key-1", "user-1", token, 201, {"id": "created"}) is True
    assert await repository.complete("key-1", "user-1", token, 201, {"id": "created"}) is False
    await repository.release("key-1", "user-1", token)
    assert await repository.delete_expired() == 2


@dataclass
class _Row:
    """A stored row: the record plus the token of the reservation that owns it."""

    record: IdempotencyRecord
    token: uuid.UUID


class FakeRepository:
    """In-memory stand-in keyed the same way as the table: (key, user_id)."""

    def __init__(self) -> None:
        self.rows: dict[tuple[str, str], _Row] = {}

    async def reserve(
        self,
        key: str,
        user_id: str,
        request_path: str,
        request_hash: str,
        expires_at: datetime,
        token: uuid.UUID,
    ) -> bool:
        existing = self.rows.get((key, user_id))
        if existing is not None and existing.record.expires_at > datetime.now(tz=UTC):
            return False
        # Mirrors ON CONFLICT ... DO UPDATE ... WHERE expires_at <= NOW(): taking over
        # an expired row installs a fresh token.
        self.rows[(key, user_id)] = _Row(
            record=IdempotencyRecord(
                key=key,
                user_id=user_id,
                request_path=request_path,
                request_hash=request_hash,
                response_status=None,
                response_body=None,
                completed=False,
                expires_at=expires_at,
            ),
            token=token,
        )
        return True

    async def get(self, key: str, user_id: str) -> IdempotencyRecord | None:
        row = self.rows.get((key, user_id))
        if row is None or row.record.expires_at <= datetime.now(tz=UTC):
            return None
        return row.record

    async def complete(
        self,
        key: str,
        user_id: str,
        token: uuid.UUID,
        response_status: int,
        response_body: dict,
    ) -> bool:
        row = self.rows.get((key, user_id))
        if row is None or row.token != token:
            return False  # reclaimed by a retry — this response is stale
        row.record = replace(
            row.record,
            completed=True,
            response_status=response_status,
            response_body=response_body,
        )
        return True

    async def release(self, key: str, user_id: str, token: uuid.UUID) -> None:
        row = self.rows.get((key, user_id))
        if row is not None and row.token == token and not row.record.completed:
            del self.rows[(key, user_id)]

    def expire(self, key: str, user_id: str) -> None:
        """Force the row past its TTL, as if the request outlived it."""
        row = self.rows[(key, user_id)]
        row.record = replace(row.record, expires_at=datetime.now(tz=UTC) - timedelta(seconds=1))


def _service(repo: FakeRepository, ttl: timedelta = timedelta(minutes=15)) -> IdempotencyService:
    return IdempotencyService(repo, ttl=ttl)


@pytest.mark.asyncio
async def test_reserve_complete_and_replay_cached_response():
    repo = FakeRepository()
    svc = _service(repo)
    request_hash = svc.body_hash({"b": 2, "a": 1})

    reservation, cached = await svc.reserve_or_get_cached(
        "key-1", "user-1", "/resource", request_hash
    )
    assert reservation is not None and cached is None
    assert await svc.complete_response(reservation, 201, {"id": "created"}) is True

    _, cached = await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash)

    assert cached is not None
    assert cached.status == 201
    assert cached.body == {"id": "created"}


@pytest.mark.asyncio
async def test_conflict_when_same_key_has_different_body():
    svc = _service(FakeRepository())

    first_hash = svc.body_hash({"name": "first"})
    second_hash = svc.body_hash({"name": "second"})
    await svc.reserve_or_get_cached("key-1", "user-1", "/resource", first_hash)

    with pytest.raises(IdempotencyConflictError):
        await svc.reserve_or_get_cached("key-1", "user-1", "/resource", second_hash)


@pytest.mark.asyncio
async def test_conflict_when_same_key_used_on_another_path():
    svc = _service(FakeRepository())
    request_hash = svc.body_hash({"name": "same"})

    await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash)

    with pytest.raises(IdempotencyConflictError):
        await svc.reserve_or_get_cached("key-1", "user-1", "/other", request_hash)


@pytest.mark.asyncio
async def test_same_key_from_another_user_is_an_independent_request():
    """Keys are scoped per user — one user's key must not block another's."""
    svc = _service(FakeRepository())
    request_hash = svc.body_hash({"name": "same"})

    first, _ = await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash)
    assert first is not None
    await svc.complete_response(first, 201, {"id": "first"})

    # user-2 reserves the same key: no conflict, no replay of user-1's response.
    second, cached = await svc.reserve_or_get_cached("key-1", "user-2", "/resource", request_hash)
    assert second is not None and cached is None
    await svc.complete_response(second, 201, {"id": "second"})

    _, first_cached = await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash)
    _, second_cached = await svc.reserve_or_get_cached("key-1", "user-2", "/resource", request_hash)
    assert first_cached is not None and first_cached.body == {"id": "first"}
    assert second_cached is not None and second_cached.body == {"id": "second"}


@pytest.mark.asyncio
async def test_in_progress_duplicate_returns_503():
    svc = _service(FakeRepository())
    request_hash = svc.body_hash({"name": "same"})

    await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash)

    with pytest.raises(web.HTTPServiceUnavailable):
        await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash)


@pytest.mark.asyncio
async def test_expired_key_is_reclaimed_rather_than_silently_unreserved():
    """An expired row must be taken over, not leave the caller running unprotected.

    Reserving against an expired row has to hand the key back to this caller; if
    it merely failed the insert, ``get`` would filter the row out by expires_at
    and the mutation would run with no pending row guarding it.
    """
    repo = FakeRepository()
    svc = _service(repo)
    request_hash = svc.body_hash({"name": "same"})

    await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash)
    repo.expire("key-1", "user-1")

    reservation, cached = await svc.reserve_or_get_cached(
        "key-1", "user-1", "/resource", request_hash
    )
    assert reservation is not None and cached is None

    row = repo.rows[("key-1", "user-1")]
    assert row.record.completed is False
    assert row.record.expires_at > datetime.now(tz=UTC)

    # The reclaimed reservation still behaves like a fresh one.
    with pytest.raises(web.HTTPServiceUnavailable):
        await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash)


@pytest.mark.asyncio
async def test_owner_that_outlived_its_ttl_cannot_complete_the_retrys_reservation():
    """The generation token fences completion to the reservation that earned it.

    A request whose handler runs past the TTL loses its row to a retry. Without
    the token it would then write its own response into the retry's reservation,
    and the retry's client would be served somebody else's result.
    """
    repo = FakeRepository()
    svc = _service(repo)
    request_hash = svc.body_hash({"name": "same"})

    stale, _ = await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash)
    assert stale is not None

    # The slow request outlives its TTL and a retry reclaims the row.
    repo.expire("key-1", "user-1")
    retry, _ = await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash)
    assert retry is not None
    assert retry.token != stale.token

    # The stale owner finally finishes — its response must not be cached.
    assert await svc.complete_response(stale, 201, {"id": "stale"}) is False

    row = repo.rows[("key-1", "user-1")]
    assert row.record.completed is False
    assert row.record.response_body is None

    # The rightful owner still completes normally.
    assert await svc.complete_response(retry, 201, {"id": "fresh"}) is True
    _, cached = await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash)
    assert cached is not None and cached.body == {"id": "fresh"}


@pytest.mark.asyncio
async def test_stale_owner_cannot_release_the_retrys_reservation():
    """Release is fenced too — a stale owner's failure must not drop the retry's row."""
    repo = FakeRepository()
    svc = _service(repo)
    request_hash = svc.body_hash({"name": "same"})

    stale, _ = await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash)
    assert stale is not None
    repo.expire("key-1", "user-1")
    retry, _ = await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash)
    assert retry is not None

    with pytest.raises(RuntimeError):
        async with svc.guard_reservation(stale):
            raise RuntimeError("slow mutation failed")

    # The retry still holds its reservation.
    assert ("key-1", "user-1") in repo.rows
    assert repo.rows[("key-1", "user-1")].token == retry.token


@pytest.mark.asyncio
async def test_row_vanishing_between_reserve_and_get_is_retried_not_waved_through():
    """Losing the insert and then reading back nothing must not look like ownership.

    The row we lose to can expire (or be swept by the cleanup worker) before we
    read it. Proceeding there would run the mutation with no pending row guarding
    it, so the service has to take another run at reserving.
    """

    class VanishingRepository(FakeRepository):
        def __init__(self, misses: int) -> None:
            super().__init__()
            self.misses = misses
            self.reserve_calls = 0

        async def reserve(self, key, user_id, request_path, request_hash, expires_at, token):
            self.reserve_calls += 1
            if self.misses > 0:
                self.misses -= 1
                return False  # somebody else holds the key…
            return await super().reserve(
                key, user_id, request_path, request_hash, expires_at, token
            )

        async def get(self, key, user_id):
            if self.misses > 0 or self.reserve_calls <= 1:
                return None  # …but it is gone by the time we read it back
            return await super().get(key, user_id)

    repo = VanishingRepository(misses=1)
    svc = _service(repo)
    request_hash = svc.body_hash({"name": "same"})

    reservation, cached = await svc.reserve_or_get_cached(
        "key-1", "user-1", "/resource", request_hash
    )

    assert reservation is not None and cached is None
    assert repo.reserve_calls == 2  # retried instead of proceeding unreserved
    assert repo.rows[("key-1", "user-1")].record.completed is False


@pytest.mark.asyncio
async def test_reservation_that_never_settles_returns_503():
    """If the row keeps vanishing, make the client retry rather than run unguarded."""

    class ChurningRepository(FakeRepository):
        async def reserve(self, key, user_id, request_path, request_hash, expires_at, token):
            return False

        async def get(self, key, user_id):
            return None

    svc = _service(ChurningRepository())

    with pytest.raises(web.HTTPServiceUnavailable):
        await svc.reserve_or_get_cached("key-1", "user-1", "/resource", svc.body_hash({"a": 1}))


@pytest.mark.asyncio
async def test_guard_releases_pending_reservation_on_error():
    repo = FakeRepository()
    svc = _service(repo)
    request_hash = svc.body_hash({"name": "same"})
    reservation, _ = await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash)

    with pytest.raises(RuntimeError):
        async with svc.guard_reservation(reservation):
            raise RuntimeError("mutation failed")

    assert repo.rows == {}


@pytest.mark.asyncio
async def test_guard_keeps_completed_record_on_error():
    repo = FakeRepository()
    svc = _service(repo)
    request_hash = svc.body_hash({"name": "same"})
    reservation, _ = await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash)
    assert reservation is not None
    await svc.complete_response(reservation, 201, {"id": "created"})

    with pytest.raises(RuntimeError):
        async with svc.guard_reservation(reservation):
            raise RuntimeError("later step failed")

    # A completed response must survive so replays still work.
    assert repo.rows[("key-1", "user-1")].record.completed is True


@pytest.mark.asyncio
async def test_guard_is_noop_without_reservation():
    repo = FakeRepository()
    svc = _service(repo)

    with pytest.raises(RuntimeError):
        async with svc.guard_reservation(None):
            raise RuntimeError("boom")

    assert repo.rows == {}
