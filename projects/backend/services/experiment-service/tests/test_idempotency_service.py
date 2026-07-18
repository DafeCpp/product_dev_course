"""Unit tests for experiment-service's IdempotencyService adapter."""
from __future__ import annotations

import hashlib
import uuid
from dataclasses import dataclass, replace
from datetime import UTC, datetime, timedelta
from uuid import uuid4

import pytest
from aiohttp import web
from backend_common.idempotency import IdempotencyRecord

from experiment_service.core.exceptions import IdempotencyConflictError
from experiment_service.services.idempotency import (
    IDEMPOTENCY_HEADER,
    IdempotencyPayload,
    IdempotencyService,
)


@dataclass
class _Row:
    record: IdempotencyRecord
    token: uuid.UUID


class MockIdempotencyRepository:
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
            # Mirrors ON CONFLICT ... DO UPDATE ... WHERE expires_at <= NOW().
            return False
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
        # Mirrors DELETE ... WHERE completed = false: completed records survive.
        row = self.rows.get((key, user_id))
        if row is not None and row.token == token and not row.record.completed:
            del self.rows[(key, user_id)]

    def expire(self, key: str, user_id) -> None:
        row = self.rows[(key, str(user_id))]
        row.record = replace(row.record, expires_at=datetime.now(tz=UTC) - timedelta(seconds=1))


PATH = "/api/v1/experiments"


class TestIdempotencyPayload:
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
    def test_canonical_body_produces_deterministic_output(self):
        _, hash1 = IdempotencyService.canonical_body({"b": 2, "a": 1})
        _, hash2 = IdempotencyService.canonical_body({"a": 1, "b": 2})
        assert hash1 == hash2

    def test_canonical_body_serializes_nested(self):
        serialized, digest = IdempotencyService.canonical_body(
            {"outer": {"b": 2, "a": 1}, "list": [3, 2, 1]}
        )
        assert '"a":1' in serialized
        assert '"b":2' in serialized
        # The shared contract is a sha256 hex string — it goes straight into a varchar(64).
        assert isinstance(digest, str)
        assert len(digest) == 64

    def test_canonical_body_handles_datetime(self):
        serialized, digest = IdempotencyService.canonical_body({"timestamp": datetime.now(tz=UTC)})
        assert serialized is not None
        assert digest is not None

    def test_canonical_body_empty_dict(self):
        serialized, digest = IdempotencyService.canonical_body({})
        assert serialized == "{}"
        assert digest == hashlib.sha256(b"{}").hexdigest()

    def test_canonical_body_different_content_different_hash(self):
        _, hash1 = IdempotencyService.canonical_body({"a": 1})
        _, hash2 = IdempotencyService.canonical_body({"a": 2})
        assert hash1 != hash2

    def test_canonical_body_matches_body_hash(self):
        body = {"a": 1, "b": [2, 3]}
        _, digest = IdempotencyService.canonical_body(body)
        assert digest == IdempotencyService.body_hash(body)


class TestIdempotencyServiceBuildResponse:
    def test_build_response_200(self):
        response = IdempotencyService.build_response(
            IdempotencyPayload(status=200, body={"success": True})
        )
        assert isinstance(response, web.Response)
        assert response.status == 200

    def test_build_response_201(self):
        response = IdempotencyService.build_response(
            IdempotencyPayload(status=201, body={"id": "123"})
        )
        assert response.status == 201

    def test_build_response_content_type(self):
        response = IdempotencyService.build_response(
            IdempotencyPayload(status=200, body={"key": "value"})
        )
        assert response.content_type == "application/json"


class TestIdempotencyServiceReserveOrGetCached:
    @pytest.mark.asyncio
    async def test_reserves_a_new_key(self):
        service = IdempotencyService(MockIdempotencyRepository())
        reservation, cached = await service.reserve_or_get_cached(
            key="new-key",
            user_id=uuid4(),
            request_path=PATH,
            body_hash=service.body_hash({"a": 1}),
        )
        assert reservation is not None
        assert cached is None

    @pytest.mark.asyncio
    async def test_returns_cached_payload_when_key_is_completed(self):
        service = IdempotencyService(MockIdempotencyRepository())
        user_id = uuid4()
        body_hash = service.body_hash({"a": 1})

        reservation, _ = await service.reserve_or_get_cached("test-key", user_id, PATH, body_hash)
        assert reservation is not None
        await service.complete_response(reservation, 201, {"result": "cached"})

        _, cached = await service.reserve_or_get_cached("test-key", user_id, PATH, body_hash)
        assert cached is not None
        assert cached.status == 201
        assert cached.body == {"result": "cached"}

    @pytest.mark.asyncio
    async def test_raises_503_when_key_is_pending(self):
        service = IdempotencyService(MockIdempotencyRepository())
        user_id = uuid4()
        body_hash = service.body_hash({"a": 1})

        await service.reserve_or_get_cached("pending-key", user_id, PATH, body_hash)

        with pytest.raises(web.HTTPServiceUnavailable):
            await service.reserve_or_get_cached("pending-key", user_id, PATH, body_hash)

    @pytest.mark.asyncio
    async def test_same_key_from_another_user_is_independent(self):
        """Keys are scoped per user: another user's key is a separate request, not a 409."""
        service = IdempotencyService(MockIdempotencyRepository())
        body_hash = service.body_hash({"a": 1})
        owner, other = uuid4(), uuid4()

        reservation, _ = await service.reserve_or_get_cached("test-key", owner, PATH, body_hash)
        assert reservation is not None
        await service.complete_response(reservation, 201, {"result": "owner"})

        # No conflict, and no replay of the owner's response.
        theirs, cached = await service.reserve_or_get_cached("test-key", other, PATH, body_hash)
        assert theirs is not None
        assert cached is None

    @pytest.mark.asyncio
    async def test_raises_conflict_on_request_path_mismatch(self):
        service = IdempotencyService(MockIdempotencyRepository())
        user_id = uuid4()
        body_hash = service.body_hash({"a": 1})

        await service.reserve_or_get_cached("test-key", user_id, PATH, body_hash)

        with pytest.raises(IdempotencyConflictError, match="belongs to another request"):
            await service.reserve_or_get_cached("test-key", user_id, "/api/different", body_hash)

    @pytest.mark.asyncio
    async def test_raises_conflict_on_body_hash_mismatch(self):
        service = IdempotencyService(MockIdempotencyRepository())
        user_id = uuid4()

        await service.reserve_or_get_cached(
            "test-key", user_id, PATH, service.body_hash({"a": 1})
        )

        with pytest.raises(IdempotencyConflictError, match="different payload"):
            await service.reserve_or_get_cached(
                "test-key", user_id, PATH, service.body_hash({"a": 2})
            )

    @pytest.mark.asyncio
    async def test_expired_reservation_is_reclaimed(self):
        """An expired row is taken over, so the retry still runs under a reservation."""
        repo = MockIdempotencyRepository()
        service = IdempotencyService(repo)
        user_id = uuid4()
        body_hash = service.body_hash({"a": 1})

        await service.reserve_or_get_cached("test-key", user_id, PATH, body_hash)
        repo.expire("test-key", user_id)

        reservation, cached = await service.reserve_or_get_cached(
            "test-key", user_id, PATH, body_hash
        )
        assert reservation is not None
        assert cached is None
        assert repo.rows[("test-key", str(user_id))].record.expires_at > datetime.now(tz=UTC)


class TestIdempotencyServiceCompleteResponse:
    @pytest.mark.asyncio
    async def test_marks_record_as_completed(self):
        repo = MockIdempotencyRepository()
        service = IdempotencyService(repo)
        user_id = uuid4()

        reservation, cached = await service.reserve_or_get_cached(
            "test-key", user_id, PATH, service.body_hash({"a": 1})
        )
        assert reservation is not None and cached is None

        response_body = {"id": "123", "created": True}
        assert await service.complete_response(reservation, 201, response_body) is True

        record = await repo.get("test-key", str(user_id))
        assert record is not None
        assert record.completed is True
        assert record.response_status == 201
        assert record.response_body == response_body

    @pytest.mark.asyncio
    async def test_owner_that_outlived_its_ttl_cannot_complete_the_retrys_reservation(self):
        """The generation token fences completion to the reservation that earned it.

        A handler that runs past the TTL loses its row to a retry. Without the token
        it would then cache its own response against the retry's reservation, and the
        retry's client would be served somebody else's result.
        """
        repo = MockIdempotencyRepository()
        service = IdempotencyService(repo)
        user_id = uuid4()
        body_hash = service.body_hash({"a": 1})

        stale, _ = await service.reserve_or_get_cached("test-key", user_id, PATH, body_hash)
        assert stale is not None

        repo.expire("test-key", user_id)
        retry, _ = await service.reserve_or_get_cached("test-key", user_id, PATH, body_hash)
        assert retry is not None
        assert retry.token != stale.token

        # The slow original finally finishes — its response must not be cached.
        assert await service.complete_response(stale, 201, {"id": "stale"}) is False
        record = await repo.get("test-key", str(user_id))
        assert record is not None
        assert record.completed is False

        # The rightful owner still completes normally.
        assert await service.complete_response(retry, 201, {"id": "fresh"}) is True
        _, cached = await service.reserve_or_get_cached("test-key", user_id, PATH, body_hash)
        assert cached is not None
        assert cached.body == {"id": "fresh"}


class TestIdempotencyServiceReleaseAndGuard:
    @pytest.mark.asyncio
    async def test_release_drops_incomplete_reservation(self):
        repo = MockIdempotencyRepository()
        service = IdempotencyService(repo)
        user_id = uuid4()

        reservation, _ = await service.reserve_or_get_cached(
            "test-key", user_id, PATH, service.body_hash({"a": 1})
        )
        assert reservation is not None
        assert await repo.get("test-key", str(user_id)) is not None

        await service.release(reservation)

        # Reservation gone — a retry may reserve the key again instead of hitting 503.
        assert await repo.get("test-key", str(user_id)) is None

    @pytest.mark.asyncio
    async def test_release_keeps_completed_record(self):
        repo = MockIdempotencyRepository()
        service = IdempotencyService(repo)
        user_id = uuid4()

        reservation, _ = await service.reserve_or_get_cached(
            "test-key", user_id, PATH, service.body_hash({"a": 1})
        )
        assert reservation is not None
        await service.complete_response(reservation, 201, {"id": "123"})

        await service.release(reservation)

        # Completed cached responses must survive release so replay still works.
        record = await repo.get("test-key", str(user_id))
        assert record is not None
        assert record.completed is True

    @pytest.mark.asyncio
    async def test_guard_releases_key_when_mutation_raises(self):
        repo = MockIdempotencyRepository()
        service = IdempotencyService(repo)
        user_id = uuid4()

        reservation, _ = await service.reserve_or_get_cached(
            "test-key", user_id, PATH, service.body_hash({"a": 1})
        )

        with pytest.raises(ValueError):
            async with service.guard_reservation(reservation):
                raise ValueError("mutation failed")

        # Poisoned reservation must be cleared so the client can retry.
        assert await repo.get("test-key", str(user_id)) is None

    @pytest.mark.asyncio
    async def test_guard_keeps_reservation_on_success(self):
        repo = MockIdempotencyRepository()
        service = IdempotencyService(repo)
        user_id = uuid4()

        reservation, _ = await service.reserve_or_get_cached(
            "test-key", user_id, PATH, service.body_hash({"a": 1})
        )

        async with service.guard_reservation(reservation):
            pass  # mutation succeeded

        # Reservation kept so complete_response can mark it done.
        assert await repo.get("test-key", str(user_id)) is not None

    @pytest.mark.asyncio
    async def test_guard_is_noop_without_reservation(self):
        repo = MockIdempotencyRepository()
        service = IdempotencyService(repo)

        # No key (request without Idempotency-Key) — guard must not touch storage.
        with pytest.raises(ValueError):
            async with service.guard_reservation(None):
                raise ValueError("boom")
        assert repo.rows == {}


class TestIdempotencyHeader:
    def test_idempotency_header_constant(self):
        assert IDEMPOTENCY_HEADER == "Idempotency-Key"


class TestIdempotencyServiceIntegration:
    @pytest.mark.asyncio
    async def test_full_idempotent_flow(self):
        service = IdempotencyService(MockIdempotencyRepository())
        user_id = uuid4()
        key = "idempotency-key-123"
        body = {"name": "Test Experiment", "project_id": str(uuid4())}
        _, body_hash = IdempotencyService.canonical_body(body)

        reservation, cached = await service.reserve_or_get_cached(key, user_id, PATH, body_hash)
        assert reservation is not None and cached is None

        response_body = {"id": str(uuid4()), "name": "Test Experiment", "status": "draft"}
        await service.complete_response(reservation, 201, response_body)

        _, cached = await service.reserve_or_get_cached(key, user_id, PATH, body_hash)
        assert cached is not None
        assert cached.status == 201
        assert cached.body == response_body
        assert IdempotencyService.build_response(cached).status == 201

    @pytest.mark.asyncio
    async def test_idempotency_with_different_bodies(self):
        service = IdempotencyService(MockIdempotencyRepository())
        user_id = uuid4()
        key = "same-key"

        _, hash1 = IdempotencyService.canonical_body({"action": "create", "value": 1})
        _, hash2 = IdempotencyService.canonical_body({"action": "create", "value": 2})

        reservation, _ = await service.reserve_or_get_cached(key, user_id, PATH, hash1)
        assert reservation is not None
        await service.complete_response(reservation, 200, {"value": 1})

        with pytest.raises(IdempotencyConflictError, match="different payload"):
            await service.reserve_or_get_cached(key, user_id, PATH, hash2)
