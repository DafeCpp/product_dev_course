"""Unit tests for config-service's IdempotencyService adapter (mocked repo)."""
from __future__ import annotations

from dataclasses import replace
from datetime import UTC, datetime, timedelta

import pytest
from aiohttp import web
from backend_common.idempotency import IdempotencyRecord
from config_service.core.exceptions import IdempotencyConflictError
from config_service.services.idempotency import IdempotencyService


class FakeRepository:
    """In-memory stand-in keyed the same way as the table: (key, user_id)."""

    def __init__(self) -> None:
        self.records: dict[tuple[str, str], IdempotencyRecord] = {}

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
        record = self.records.get((key, user_id))
        if record is not None and not record.completed:
            del self.records[(key, user_id)]


PATH = "/api/v1/config"


def test_body_hash_is_deterministic():
    body = {"service_name": "svc", "key": "k", "value": {"enabled": True}}
    assert IdempotencyService.body_hash(body) == IdempotencyService.body_hash(body)


def test_body_hash_differs_for_different_bodies():
    assert IdempotencyService.body_hash({"enabled": True}) != IdempotencyService.body_hash(
        {"enabled": False}
    )


def test_body_hash_is_order_independent():
    assert IdempotencyService.body_hash({"a": 1, "b": 2}) == IdempotencyService.body_hash(
        {"b": 2, "a": 1}
    )


@pytest.mark.asyncio
async def test_new_key_is_reserved_and_returns_none():
    svc = IdempotencyService(FakeRepository())
    body_hash = svc.body_hash({"key": "k"})

    assert await svc.reserve_or_get_cached("key-1", "user-1", PATH, body_hash) is None


@pytest.mark.asyncio
async def test_completed_key_replays_cached_response():
    svc = IdempotencyService(FakeRepository())
    body_hash = svc.body_hash({"key": "k"})

    await svc.reserve_or_get_cached("key-1", "user-1", PATH, body_hash)
    await svc.complete_response("key-1", "user-1", 201, {"id": "abc"})

    cached = await svc.reserve_or_get_cached("key-1", "user-1", PATH, body_hash)
    assert cached is not None
    assert cached.status == 201
    assert cached.body == {"id": "abc"}


@pytest.mark.asyncio
async def test_conflict_on_different_body_hash():
    svc = IdempotencyService(FakeRepository())

    await svc.reserve_or_get_cached("key-1", "user-1", PATH, svc.body_hash({"v": 1}))

    with pytest.raises(IdempotencyConflictError):
        await svc.reserve_or_get_cached("key-1", "user-1", PATH, svc.body_hash({"v": 2}))


@pytest.mark.asyncio
async def test_in_progress_duplicate_returns_503():
    svc = IdempotencyService(FakeRepository())
    body_hash = svc.body_hash({"key": "k"})

    await svc.reserve_or_get_cached("key-1", "user-1", PATH, body_hash)

    with pytest.raises(web.HTTPServiceUnavailable):
        await svc.reserve_or_get_cached("key-1", "user-1", PATH, body_hash)


@pytest.mark.asyncio
async def test_different_user_same_key_is_independent():
    """Documented config-service behaviour: a key belongs to its user, not globally."""
    svc = IdempotencyService(FakeRepository())
    body_hash = svc.body_hash({"key": "k"})

    await svc.reserve_or_get_cached("key-1", "user-1", PATH, body_hash)
    await svc.complete_response("key-1", "user-1", 201, {"id": "first"})

    # user-2 must not get user-1's cached response, and must not get a 409 either.
    assert await svc.reserve_or_get_cached("key-1", "user-2", PATH, body_hash) is None


@pytest.mark.asyncio
async def test_guard_releases_reservation_when_mutation_fails():
    repo = FakeRepository()
    svc = IdempotencyService(repo)
    body_hash = svc.body_hash({"key": "k"})

    await svc.reserve_or_get_cached("key-1", "user-1", PATH, body_hash)

    with pytest.raises(RuntimeError):
        async with svc.guard_reservation("key-1", "user-1"):
            raise RuntimeError("create failed")

    # Retry must be able to reserve again rather than hit a stuck 503.
    assert await svc.reserve_or_get_cached("key-1", "user-1", PATH, body_hash) is None


@pytest.mark.asyncio
async def test_ttl_comes_from_settings():
    repo = FakeRepository()
    svc = IdempotencyService(repo)
    ttl = timedelta(minutes=15)  # config_service.settings.idempotency_ttl_minutes

    before = datetime.now(tz=UTC)
    await svc.reserve_or_get_cached("key-1", "user-1", PATH, svc.body_hash({"key": "k"}))
    after = datetime.now(tz=UTC)

    expires_at = repo.records[("key-1", "user-1")].expires_at
    assert before + ttl <= expires_at <= after + ttl
