from __future__ import annotations

from dataclasses import replace
from datetime import UTC, datetime, timedelta

import pytest
from aiohttp import web

from backend_common.idempotency import (
    IdempotencyConflictError,
    IdempotencyRecord,
    IdempotencyService,
)


class FakeRepository:
    """In-memory stand-in keyed the same way as the table: (key, user_id)."""

    def __init__(self) -> None:
        self.records: dict[tuple[str, str], IdempotencyRecord] = {}
        self.released: list[tuple[str, str]] = []

    async def reserve(
        self,
        key: str,
        user_id: str,
        request_path: str,
        request_hash: str,
        expires_at: datetime,
    ) -> bool:
        existing = self.records.get((key, user_id))
        if existing is not None and existing.expires_at > datetime.now(tz=UTC):
            return False
        # Mirrors the ON CONFLICT ... DO UPDATE ... WHERE expires_at <= NOW() takeover.
        self.records[(key, user_id)] = IdempotencyRecord(
            key=key,
            user_id=user_id,
            request_path=request_path,
            request_hash=request_hash,
            response_status=None,
            response_body=None,
            completed=False,
            expires_at=expires_at,
        )
        return True

    async def get(self, key: str, user_id: str) -> IdempotencyRecord | None:
        record = self.records.get((key, user_id))
        if record is None or record.expires_at <= datetime.now(tz=UTC):
            return None
        return record

    async def complete(
        self, key: str, user_id: str, response_status: int, response_body: dict
    ) -> None:
        record = self.records[(key, user_id)]
        self.records[(key, user_id)] = replace(
            record,
            completed=True,
            response_status=response_status,
            response_body=response_body,
        )

    async def release(self, key: str, user_id: str) -> None:
        self.released.append((key, user_id))
        record = self.records.get((key, user_id))
        if record is not None and not record.completed:
            del self.records[(key, user_id)]


def _service(repo: FakeRepository, ttl: timedelta = timedelta(minutes=15)) -> IdempotencyService:
    return IdempotencyService(repo, ttl=ttl)


@pytest.mark.asyncio
async def test_reserve_complete_and_replay_cached_response():
    repo = FakeRepository()
    svc = _service(repo)
    request_hash = svc.body_hash({"b": 2, "a": 1})

    assert await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash) is None
    await svc.complete_response("key-1", "user-1", 201, {"id": "created"})

    cached = await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash)

    assert cached is not None
    assert cached.status == 201
    assert cached.body == {"id": "created"}


@pytest.mark.asyncio
async def test_conflict_when_same_key_has_different_body():
    repo = FakeRepository()
    svc = _service(repo)

    first_hash = svc.body_hash({"name": "first"})
    second_hash = svc.body_hash({"name": "second"})
    assert await svc.reserve_or_get_cached("key-1", "user-1", "/resource", first_hash) is None

    with pytest.raises(IdempotencyConflictError):
        await svc.reserve_or_get_cached("key-1", "user-1", "/resource", second_hash)


@pytest.mark.asyncio
async def test_conflict_when_same_key_used_on_another_path():
    repo = FakeRepository()
    svc = _service(repo)
    request_hash = svc.body_hash({"name": "same"})

    assert await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash) is None

    with pytest.raises(IdempotencyConflictError):
        await svc.reserve_or_get_cached("key-1", "user-1", "/other", request_hash)


@pytest.mark.asyncio
async def test_same_key_from_another_user_is_an_independent_request():
    """Keys are scoped per user — one user's key must not block another's."""
    repo = FakeRepository()
    svc = _service(repo)
    request_hash = svc.body_hash({"name": "same"})

    assert await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash) is None
    await svc.complete_response("key-1", "user-1", 201, {"id": "first"})

    # user-2 reserves the same key: no conflict, no replay of user-1's response.
    assert await svc.reserve_or_get_cached("key-1", "user-2", "/resource", request_hash) is None
    await svc.complete_response("key-1", "user-2", 201, {"id": "second"})

    first = await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash)
    second = await svc.reserve_or_get_cached("key-1", "user-2", "/resource", request_hash)
    assert first is not None and first.body == {"id": "first"}
    assert second is not None and second.body == {"id": "second"}


@pytest.mark.asyncio
async def test_in_progress_duplicate_returns_503():
    repo = FakeRepository()
    svc = _service(repo)
    request_hash = svc.body_hash({"name": "same"})

    assert await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash) is None

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
    expired = _service(repo, ttl=timedelta(seconds=-1))
    request_hash = expired.body_hash({"name": "same"})

    assert await expired.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash) is None

    svc = _service(repo)
    assert await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash) is None

    record = repo.records[("key-1", "user-1")]
    assert record.completed is False
    assert record.expires_at > datetime.now(tz=UTC)

    # The reclaimed reservation still behaves like a fresh one.
    with pytest.raises(web.HTTPServiceUnavailable):
        await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash)


@pytest.mark.asyncio
async def test_row_vanishing_between_reserve_and_get_is_retried_not_waved_through():
    """Losing the insert and then reading back nothing must not look like ownership.

    The row we lose to can expire (or be swept by the cleanup worker) before we
    read it. Returning None there would run the mutation with no pending row
    guarding it, so the service has to take another run at reserving.
    """

    class VanishingRepository(FakeRepository):
        def __init__(self, misses: int) -> None:
            super().__init__()
            self.misses = misses
            self.reserve_calls = 0

        async def reserve(self, key, user_id, request_path, request_hash, expires_at):
            self.reserve_calls += 1
            if self.misses > 0:
                self.misses -= 1
                return False  # somebody else holds the key…
            return await super().reserve(key, user_id, request_path, request_hash, expires_at)

        async def get(self, key, user_id):
            if self.misses > 0 or self.reserve_calls <= 1:
                return None  # …but it is gone by the time we read it back
            return await super().get(key, user_id)

    repo = VanishingRepository(misses=1)
    svc = _service(repo)
    request_hash = svc.body_hash({"name": "same"})

    assert await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash) is None

    assert repo.reserve_calls == 2  # retried instead of proceeding unreserved
    assert repo.records[("key-1", "user-1")].completed is False


@pytest.mark.asyncio
async def test_reservation_that_never_settles_returns_503():
    """If the row keeps vanishing, make the client retry rather than run unguarded."""

    class ChurningRepository(FakeRepository):
        async def reserve(self, key, user_id, request_path, request_hash, expires_at):
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
    await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash)

    with pytest.raises(RuntimeError):
        async with svc.guard_reservation("key-1", "user-1"):
            raise RuntimeError("mutation failed")

    assert repo.released == [("key-1", "user-1")]
    assert repo.records == {}


@pytest.mark.asyncio
async def test_guard_keeps_completed_record_on_error():
    repo = FakeRepository()
    svc = _service(repo)
    request_hash = svc.body_hash({"name": "same"})
    await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash)
    await svc.complete_response("key-1", "user-1", 201, {"id": "created"})

    with pytest.raises(RuntimeError):
        async with svc.guard_reservation("key-1", "user-1"):
            raise RuntimeError("later step failed")

    # A completed response must survive so replays still work.
    assert repo.records[("key-1", "user-1")].completed is True


@pytest.mark.asyncio
async def test_guard_is_noop_without_key():
    repo = FakeRepository()
    svc = _service(repo)

    with pytest.raises(RuntimeError):
        async with svc.guard_reservation(None, "user-1"):
            raise RuntimeError("boom")

    assert repo.released == []
