from __future__ import annotations

from dataclasses import replace
from datetime import datetime, timedelta

import pytest
from aiohttp import web

from backend_common.idempotency import IdempotencyConflictError, IdempotencyRecord, IdempotencyService


class FakeRepository:
    def __init__(self) -> None:
        self.record: IdempotencyRecord | None = None
        self.released: list[str] = []

    async def reserve(self, key: str, user_id: str, request_path: str, request_hash: str, expires_at: datetime) -> bool:
        if self.record is not None:
            return False
        self.record = IdempotencyRecord(
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

    async def get(self, key: str) -> IdempotencyRecord | None:
        return self.record if self.record and self.record.key == key else None

    async def complete(self, key: str, response_status: int, response_body: dict) -> None:
        assert self.record is not None
        self.record = replace(
            self.record,
            completed=True,
            response_status=response_status,
            response_body=response_body,
        )

    async def release(self, key: str) -> None:
        self.released.append(key)
        if self.record and self.record.key == key and not self.record.completed:
            self.record = None


@pytest.mark.asyncio
async def test_reserve_complete_and_replay_cached_response():
    repo = FakeRepository()
    svc = IdempotencyService(repo, ttl=timedelta(minutes=15))
    request_hash = svc.body_hash({"b": 2, "a": 1})

    assert await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash) is None
    await svc.complete_response("key-1", 201, {"id": "created"})

    cached = await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash)

    assert cached is not None
    assert cached.status == 201
    assert cached.body == {"id": "created"}


@pytest.mark.asyncio
async def test_conflict_when_same_key_has_different_body():
    repo = FakeRepository()
    svc = IdempotencyService(repo, ttl=timedelta(minutes=15))

    first_hash = svc.body_hash({"name": "first"})
    second_hash = svc.body_hash({"name": "second"})
    assert await svc.reserve_or_get_cached("key-1", "user-1", "/resource", first_hash) is None

    with pytest.raises(IdempotencyConflictError):
        await svc.reserve_or_get_cached("key-1", "user-1", "/resource", second_hash)


@pytest.mark.asyncio
async def test_in_progress_duplicate_returns_503():
    repo = FakeRepository()
    svc = IdempotencyService(repo, ttl=timedelta(minutes=15))
    request_hash = svc.body_hash({"name": "same"})

    assert await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash) is None

    with pytest.raises(web.HTTPServiceUnavailable):
        await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash)


@pytest.mark.asyncio
async def test_guard_releases_pending_reservation_on_error():
    repo = FakeRepository()
    svc = IdempotencyService(repo, ttl=timedelta(minutes=15))
    request_hash = svc.body_hash({"name": "same"})
    await svc.reserve_or_get_cached("key-1", "user-1", "/resource", request_hash)

    with pytest.raises(RuntimeError):
        async with svc.guard_reservation("key-1"):
            raise RuntimeError("mutation failed")

    assert repo.released == ["key-1"]
    assert repo.record is None
