"""Unit tests for IdempotencyService (mocked repo)."""
from __future__ import annotations

from datetime import datetime, timedelta, timezone
from unittest.mock import AsyncMock, MagicMock
from uuid import uuid4

import pytest

from config_service.core.exceptions import IdempotencyConflictError
from config_service.domain.models import IdempotencyRecord
from config_service.services.idempotency import IdempotencyService


def _make_record(key: str, user_id: str, path: str, body_hash: str) -> IdempotencyRecord:
    return IdempotencyRecord(
        id=uuid4(),
        idempotency_key=key,
        user_id=user_id,
        request_path=path,
        request_hash=body_hash,
        response_status=201,
        response_body={"id": str(uuid4())},
        expires_at=datetime.now(tz=timezone.utc) + timedelta(minutes=15),
        created_at=datetime.now(tz=timezone.utc),
    )


def test_body_hash_is_deterministic():
    body = {"service_name": "svc", "key": "k", "value": {"enabled": True}}
    h1 = IdempotencyService.body_hash(body)
    h2 = IdempotencyService.body_hash(body)
    assert h1 == h2


def test_body_hash_differs_for_different_bodies():
    h1 = IdempotencyService.body_hash({"enabled": True})
    h2 = IdempotencyService.body_hash({"enabled": False})
    assert h1 != h2


def test_body_hash_is_order_independent():
    h1 = IdempotencyService.body_hash({"a": 1, "b": 2})
    h2 = IdempotencyService.body_hash({"b": 2, "a": 1})
    assert h1 == h2


@pytest.mark.asyncio
async def test_no_cached_response_returns_none():
    repo = MagicMock()
    repo.get = AsyncMock(return_value=None)
    svc = IdempotencyService(repo)
    result = await svc.get_cached_response("key", "user", "/path", "hash")
    assert result is None


@pytest.mark.asyncio
async def test_cached_response_same_hash_returns_payload():
    key = "my-key"
    user_id = "user-1"
    path = "/api/v1/config"
    body_hash = "abc123"

    record = _make_record(key, user_id, path, body_hash)
    repo = MagicMock()
    repo.get = AsyncMock(return_value=record)
    svc = IdempotencyService(repo)

    result = await svc.get_cached_response(key, user_id, path, body_hash)
    assert result is not None
    assert result.status == 201


@pytest.mark.asyncio
async def test_conflict_on_different_body_hash():
    key = "my-key"
    user_id = "user-1"
    path = "/api/v1/config"
    record = _make_record(key, user_id, path, "hash-original")

    repo = MagicMock()
    repo.get = AsyncMock(return_value=record)
    svc = IdempotencyService(repo)

    with pytest.raises(IdempotencyConflictError):
        await svc.get_cached_response(key, user_id, path, "hash-different")


@pytest.mark.asyncio
async def test_different_user_same_key_returns_none():
    """Different user with the same idempotency key is treated as independent — no conflict."""
    key = "my-key"
    path = "/api/v1/config"
    body_hash = "same-hash"
    record = _make_record(key, "user-1", path, body_hash)

    repo = MagicMock()
    repo.get = AsyncMock(return_value=record)
    svc = IdempotencyService(repo)

    result = await svc.get_cached_response(key, "user-2", path, body_hash)
    assert result is None
