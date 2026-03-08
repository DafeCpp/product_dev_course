"""Unit tests for IdempotencyService."""
from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Any
from uuid import UUID, uuid4

import pytest
from aiohttp import web

from experiment_service.core.exceptions import IdempotencyConflictError
from experiment_service.repositories.idempotency import IdempotencyRecord, IdempotencyRepository
from experiment_service.services.idempotency import (
    IDEMPOTENCY_HEADER,
    IdempotencyPayload,
    IdempotencyService,
)


# Mock repository for unit testing
class MockIdempotencyRepository:
    """In-memory mock for IdempotencyRepository."""

    def __init__(self) -> None:
        self._storage: dict[str, IdempotencyRecord] = {}
        self._deleted_count: int = 0

    async def get(self, key: str) -> IdempotencyRecord | None:
        return self._storage.get(key)

    async def save(
        self,
        key: str,
        user_id: UUID,
        request_path: str,
        request_body_hash: bytes,
        response_status: int,
        response_body: dict,
    ) -> None:
        if key not in self._storage:
            self._storage[key] = IdempotencyRecord(
                key=key,
                user_id=user_id,
                request_path=request_path,
                request_body_hash=request_body_hash,
                response_status=response_status,
                response_body=response_body,
            )

    async def delete_expired(self, created_before: datetime) -> int:
        # Simplified: just clear all and return count
        count = len(self._storage)
        self._storage.clear()
        self._deleted_count = count
        return count

    def set_record(self, record: IdempotencyRecord) -> None:
        self._storage[record.key] = record


class TestIdempotencyPayload:
    """Tests for IdempotencyPayload dataclass."""

    def test_create_payload(self):
        payload = IdempotencyPayload(status=200, body={"id": "123"})
        assert payload.status == 200
        assert payload.body == {"id": "123"}

    def test_payload_with_complex_body(self):
        body = {"nested": {"key": "value"}, "list": [1, 2, 3]}
        payload = IdempotencyPayload(status=201, body=body)
        assert payload.body["nested"]["key"] == "value"
        assert payload.body["list"] == [1, 2, 3]


class TestIdempotencyServiceCanonicalBody:
    """Tests for IdempotencyService.canonical_body static method."""

    def test_canonical_body_produces_deterministic_output(self):
        body1 = {"b": 2, "a": 1}
        body2 = {"a": 1, "b": 2}
        _, hash1 = IdempotencyService.canonical_body(body1)
        _, hash2 = IdempotencyService.canonical_body(body2)
        assert hash1 == hash2

    def test_canonical_body_serializes_nested(self):
        body = {"outer": {"b": 2, "a": 1}, "list": [3, 2, 1]}
        serialized, digest = IdempotencyService.canonical_body(body)
        assert '"a":1' in serialized
        assert '"b":2' in serialized
        assert isinstance(digest, bytes)
        assert len(digest) == 32  # SHA256

    def test_canonical_body_handles_datetime(self):
        now = datetime.now(timezone.utc)
        body = {"timestamp": now}
        serialized, digest = IdempotencyService.canonical_body(body)
        assert serialized is not None
        assert digest is not None

    def test_canonical_body_empty_dict(self):
        serialized, digest = IdempotencyService.canonical_body({})
        assert serialized == "{}"
        assert digest == hashlib.sha256(b"{}").digest()

    def test_canonical_body_different_order_same_hash(self):
        body1 = {"z": 1, "a": 2, "m": 3}
        body2 = {"a": 2, "m": 3, "z": 1}
        _, hash1 = IdempotencyService.canonical_body(body1)
        _, hash2 = IdempotencyService.canonical_body(body2)
        assert hash1 == hash2

    def test_canonical_body_different_content_different_hash(self):
        body1 = {"a": 1}
        body2 = {"a": 2}
        _, hash1 = IdempotencyService.canonical_body(body1)
        _, hash2 = IdempotencyService.canonical_body(body2)
        assert hash1 != hash2


class TestIdempotencyServiceBuildResponse:
    """Tests for IdempotencyService.build_response static method."""

    def test_build_response_200(self):
        payload = IdempotencyPayload(status=200, body={"success": True})
        response = IdempotencyService.build_response(payload)
        assert isinstance(response, web.Response)
        assert response.status == 200

    def test_build_response_201(self):
        payload = IdempotencyPayload(status=201, body={"id": "123"})
        response = IdempotencyService.build_response(payload)
        assert response.status == 201

    def test_build_response_content_type(self):
        payload = IdempotencyPayload(status=200, body={"key": "value"})
        response = IdempotencyService.build_response(payload)
        assert response.content_type == "application/json"


class TestIdempotencyServiceGetCachedResponse:
    """Tests for IdempotencyService.get_cached_response method."""

    @pytest.mark.asyncio
    async def test_returns_none_when_key_not_found(self):
        repo = MockIdempotencyRepository()
        service = IdempotencyService(repo)
        result = await service.get_cached_response(
            key="nonexistent",
            user_id=uuid4(),
            request_path="/api/test",
            body_hash=b"hash",
        )
        assert result is None

    @pytest.mark.asyncio
    async def test_returns_payload_when_key_found(self):
        repo = MockIdempotencyRepository()
        user_id = uuid4()
        key = "test-key"
        record = IdempotencyRecord(
            key=key,
            user_id=user_id,
            request_path="/api/test",
            request_body_hash=b"hash",
            response_status=200,
            response_body={"result": "cached"},
        )
        repo.set_record(record)
        service = IdempotencyService(repo)

        result = await service.get_cached_response(
            key=key,
            user_id=user_id,
            request_path="/api/test",
            body_hash=b"hash",
        )

        assert result is not None
        assert result.status == 200
        assert result.body == {"result": "cached"}

    @pytest.mark.asyncio
    async def test_raises_on_user_id_mismatch(self):
        repo = MockIdempotencyRepository()
        user_id = uuid4()
        different_user_id = uuid4()
        key = "test-key"
        record = IdempotencyRecord(
            key=key,
            user_id=user_id,
            request_path="/api/test",
            request_body_hash=b"hash",
            response_status=200,
            response_body={"result": "cached"},
        )
        repo.set_record(record)
        service = IdempotencyService(repo)

        with pytest.raises(IdempotencyConflictError, match="belongs to another request"):
            await service.get_cached_response(
                key=key,
                user_id=different_user_id,
                request_path="/api/test",
                body_hash=b"hash",
            )

    @pytest.mark.asyncio
    async def test_raises_on_request_path_mismatch(self):
        repo = MockIdempotencyRepository()
        user_id = uuid4()
        key = "test-key"
        record = IdempotencyRecord(
            key=key,
            user_id=user_id,
            request_path="/api/test",
            request_body_hash=b"hash",
            response_status=200,
            response_body={"result": "cached"},
        )
        repo.set_record(record)
        service = IdempotencyService(repo)

        with pytest.raises(IdempotencyConflictError, match="belongs to another request"):
            await service.get_cached_response(
                key=key,
                user_id=user_id,
                request_path="/api/different",
                body_hash=b"hash",
            )

    @pytest.mark.asyncio
    async def test_raises_on_body_hash_mismatch(self):
        repo = MockIdempotencyRepository()
        user_id = uuid4()
        key = "test-key"
        record = IdempotencyRecord(
            key=key,
            user_id=user_id,
            request_path="/api/test",
            request_body_hash=b"original-hash",
            response_status=200,
            response_body={"result": "cached"},
        )
        repo.set_record(record)
        service = IdempotencyService(repo)

        with pytest.raises(IdempotencyConflictError, match="different payload"):
            await service.get_cached_response(
                key=key,
                user_id=user_id,
                request_path="/api/test",
                body_hash=b"different-hash",
            )


class TestIdempotencyServiceStoreResponse:
    """Tests for IdempotencyService.store_response method."""

    @pytest.mark.asyncio
    async def test_stores_response_correctly(self):
        repo = MockIdempotencyRepository()
        service = IdempotencyService(repo)
        user_id = uuid4()
        key = "test-key"
        request_path = "/api/test"
        body_hash = b"test-hash"
        response_status = 201
        response_body = {"id": "123", "created": True}

        await service.store_response(
            key=key,
            user_id=user_id,
            request_path=request_path,
            body_hash=body_hash,
            response_status=response_status,
            response_body=response_body,
        )

        record = await repo.get(key)
        assert record is not None
        assert record.key == key
        assert record.user_id == user_id
        assert record.request_path == request_path
        assert record.request_body_hash == body_hash
        assert record.response_status == response_status
        assert record.response_body == response_body

    @pytest.mark.asyncio
    async def test_does_not_overwrite_existing_key(self):
        repo = MockIdempotencyRepository()
        service = IdempotencyService(repo)
        user_id = uuid4()
        key = "test-key"

        # Store first response
        await service.store_response(
            key=key,
            user_id=user_id,
            request_path="/api/test",
            body_hash=b"hash1",
            response_status=200,
            response_body={"first": True},
        )

        # Try to store second response with same key
        await service.store_response(
            key=key,
            user_id=user_id,
            request_path="/api/test",
            body_hash=b"hash2",
            response_status=201,
            response_body={"second": True},
        )

        # Should still have first response (ON CONFLICT DO NOTHING)
        record = await repo.get(key)
        assert record is not None
        assert record.response_body == {"first": True}
        assert record.response_status == 200


class TestIdempotencyServiceAssertRecord:
    """Tests for IdempotencyService._assert_record static method."""

    def test_no_raise_on_matching_record(self):
        user_id = uuid4()
        record = IdempotencyRecord(
            key="test-key",
            user_id=user_id,
            request_path="/api/test",
            request_body_hash=b"hash",
            response_status=200,
            response_body={},
        )
        # Should not raise
        IdempotencyService._assert_record(record, user_id, "/api/test", b"hash")

    def test_raises_on_user_id_mismatch(self):
        user_id = uuid4()
        different_user_id = uuid4()
        record = IdempotencyRecord(
            key="test-key",
            user_id=user_id,
            request_path="/api/test",
            request_body_hash=b"hash",
            response_status=200,
            response_body={},
        )
        with pytest.raises(IdempotencyConflictError, match="belongs to another request"):
            IdempotencyService._assert_record(record, different_user_id, "/api/test", b"hash")

    def test_raises_on_request_path_mismatch(self):
        user_id = uuid4()
        record = IdempotencyRecord(
            key="test-key",
            user_id=user_id,
            request_path="/api/test",
            request_body_hash=b"hash",
            response_status=200,
            response_body={},
        )
        with pytest.raises(IdempotencyConflictError, match="belongs to another request"):
            IdempotencyService._assert_record(record, user_id, "/api/different", b"hash")

    def test_raises_on_body_hash_mismatch(self):
        user_id = uuid4()
        record = IdempotencyRecord(
            key="test-key",
            user_id=user_id,
            request_path="/api/test",
            request_body_hash=b"original-hash",
            response_status=200,
            response_body={},
        )
        with pytest.raises(IdempotencyConflictError, match="different payload"):
            IdempotencyService._assert_record(record, user_id, "/api/test", b"different-hash")


class TestIdempotencyHeader:
    """Tests for idempotency header constant."""

    def test_idempotency_header_constant(self):
        assert IDEMPOTENCY_HEADER == "Idempotency-Key"


class TestIdempotencyServiceIntegration:
    """Integration-style tests for IdempotencyService."""

    @pytest.mark.asyncio
    async def test_full_idempotent_flow(self):
        repo = MockIdempotencyRepository()
        service = IdempotencyService(repo)
        user_id = uuid4()
        key = "idempotency-key-123"
        request_path = "/api/v1/experiments"
        body = {"name": "Test Experiment", "project_id": str(uuid4())}
        _, body_hash = IdempotencyService.canonical_body(body)

        # First request - no cached response
        cached = await service.get_cached_response(key, user_id, request_path, body_hash)
        assert cached is None

        # Store response
        response_body = {"id": str(uuid4()), "name": "Test Experiment", "status": "draft"}
        await service.store_response(
            key=key,
            user_id=user_id,
            request_path=request_path,
            body_hash=body_hash,
            response_status=201,
            response_body=response_body,
        )

        # Second request - should return cached response
        cached = await service.get_cached_response(key, user_id, request_path, body_hash)
        assert cached is not None
        assert cached.status == 201
        assert cached.body == response_body

        # Build HTTP response
        http_response = IdempotencyService.build_response(cached)
        assert http_response.status == 201

    @pytest.mark.asyncio
    async def test_idempotency_with_different_bodies(self):
        repo = MockIdempotencyRepository()
        service = IdempotencyService(repo)
        user_id = uuid4()
        key = "same-key"
        request_path = "/api/test"

        body1 = {"action": "create", "value": 1}
        body2 = {"action": "create", "value": 2}
        _, hash1 = IdempotencyService.canonical_body(body1)
        _, hash2 = IdempotencyService.canonical_body(body2)

        # Store first response
        await service.store_response(
            key=key,
            user_id=user_id,
            request_path=request_path,
            body_hash=hash1,
            response_status=200,
            response_body={"value": 1},
        )

        # Second request with different body should raise conflict
        with pytest.raises(IdempotencyConflictError, match="different payload"):
            await service.get_cached_response(key, user_id, request_path, hash2)
