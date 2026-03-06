"""Unit tests for telemetry_ingest_service.services.profile_cache module."""
from __future__ import annotations

import asyncio
import json
import time
from unittest.mock import AsyncMock, MagicMock, patch
from uuid import uuid4

import pytest

from telemetry_ingest_service.services.profile_cache import (
    CachedProfile,
    ProfileCache,
    profile_cache,
)
from telemetry_ingest_service.settings import settings


class TestCachedProfile:
    """Tests for CachedProfile dataclass."""

    def test_creation(self):
        """Test CachedProfile creation."""
        profile_id = uuid4()
        profile = CachedProfile(
            profile_id=profile_id,
            kind="linear",
            payload={"a": 1.0, "b": 2.0},
        )

        assert profile.profile_id == profile_id
        assert profile.kind == "linear"
        assert profile.payload == {"a": 1.0, "b": 2.0}

    def test_frozen(self):
        """Test CachedProfile is frozen (immutable)."""
        profile_id = uuid4()
        profile = CachedProfile(
            profile_id=profile_id,
            kind="linear",
            payload={"a": 1.0},
        )

        with pytest.raises(Exception):  # frozen dataclass raises
            profile.kind = "polynomial"

    def test_slots(self):
        """Test CachedProfile uses slots."""
        profile_id = uuid4()
        profile = CachedProfile(
            profile_id=profile_id,
            kind="linear",
            payload={"a": 1.0},
        )

        # Should not have __dict__
        assert not hasattr(profile, "__dict__")

    def test_with_complex_payload(self):
        """Test CachedProfile with complex payload."""
        profile_id = uuid4()
        complex_payload = {
            "coefficients": [1.0, 2.0, 3.0],
            "nested": {"key": "value"},
            "list": [1, 2, 3],
        }

        profile = CachedProfile(
            profile_id=profile_id,
            kind="polynomial",
            payload=complex_payload,
        )

        assert profile.payload == complex_payload

    def test_equality(self):
        """Test CachedProfile equality."""
        profile_id = uuid4()

        profile1 = CachedProfile(
            profile_id=profile_id,
            kind="linear",
            payload={"a": 1.0},
        )

        profile2 = CachedProfile(
            profile_id=profile_id,
            kind="linear",
            payload={"a": 1.0},
        )

        profile3 = CachedProfile(
            profile_id=profile_id,
            kind="linear",
            payload={"a": 2.0},
        )

        assert profile1 == profile2
        assert profile1 != profile3


class TestProfileCacheCreation:
    """Tests for ProfileCache initialization."""

    def test_default_ttl(self):
        """Test ProfileCache with default TTL."""
        cache = ProfileCache(ttl_seconds=60.0)
        assert cache._ttl == 60.0
        assert cache._cache == {}

    def test_custom_ttl(self):
        """Test ProfileCache with custom TTL."""
        cache = ProfileCache(ttl_seconds=120.0)
        assert cache._ttl == 120.0

    def test_zero_ttl(self):
        """Test ProfileCache with zero TTL (always expires)."""
        cache = ProfileCache(ttl_seconds=0.0)
        assert cache._ttl == 0.0

    def test_negative_ttl(self):
        """Test ProfileCache with negative TTL."""
        cache = ProfileCache(ttl_seconds=-10.0)
        assert cache._ttl == -10.0


class TestProfileCacheGetActiveProfile:
    """Tests for ProfileCache.get_active_profile method."""

    @pytest.mark.asyncio
    async def test_cache_miss_loads_from_db(self):
        """Test cache miss loads from database."""
        cache = ProfileCache(ttl_seconds=60.0)
        sensor_id = uuid4()

        mock_conn = AsyncMock()
        mock_row = {
            "id": uuid4(),
            "kind": "linear",
            "payload": {"a": 1.0, "b": 2.0},
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        profile = await cache.get_active_profile(mock_conn, sensor_id)

        assert profile is not None
        assert profile.kind == "linear"
        assert profile.payload == {"a": 1.0, "b": 2.0}
        mock_conn.fetchrow.assert_called_once()

    @pytest.mark.asyncio
    async def test_cache_hit_returns_cached_value(self):
        """Test cache hit returns cached value without DB call."""
        cache = ProfileCache(ttl_seconds=60.0)
        sensor_id = uuid4()
        profile_id = uuid4()

        # Pre-populate cache
        cached_profile = CachedProfile(
            profile_id=profile_id,
            kind="linear",
            payload={"a": 1.0},
        )
        cache._cache[sensor_id] = (cached_profile, time.monotonic() + 60.0)

        mock_conn = AsyncMock()

        profile = await cache.get_active_profile(mock_conn, sensor_id)

        assert profile == cached_profile
        mock_conn.fetchrow.assert_not_called()

    @pytest.mark.asyncio
    async def test_cache_expired_loads_from_db(self):
        """Test expired cache entry loads from database."""
        cache = ProfileCache(ttl_seconds=60.0)
        sensor_id = uuid4()

        # Pre-populate with expired entry
        old_profile = CachedProfile(
            profile_id=uuid4(),
            kind="old",
            payload={"old": "data"},
        )
        cache._cache[sensor_id] = (old_profile, time.monotonic() - 1.0)  # Expired

        mock_conn = AsyncMock()
        new_row = {
            "id": uuid4(),
            "kind": "new",
            "payload": {"new": "data"},
        }
        mock_conn.fetchrow = AsyncMock(return_value=new_row)

        profile = await cache.get_active_profile(mock_conn, sensor_id)

        assert profile is not None
        assert profile.kind == "new"
        mock_conn.fetchrow.assert_called_once()

    @pytest.mark.asyncio
    async def test_cache_stores_loaded_value(self):
        """Test loaded value is stored in cache."""
        cache = ProfileCache(ttl_seconds=60.0)
        sensor_id = uuid4()

        mock_conn = AsyncMock()
        mock_row = {
            "id": uuid4(),
            "kind": "linear",
            "payload": {"a": 1.0},
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        await cache.get_active_profile(mock_conn, sensor_id)

        assert sensor_id in cache._cache
        cached_profile, expiry = cache._cache[sensor_id]
        assert cached_profile.kind == "linear"
        assert expiry > time.monotonic()

    @pytest.mark.asyncio
    async def test_cache_stores_none_for_missing_profile(self):
        """Test None is cached when profile doesn't exist."""
        cache = ProfileCache(ttl_seconds=60.0)
        sensor_id = uuid4()

        mock_conn = AsyncMock()
        mock_conn.fetchrow = AsyncMock(return_value=None)

        profile = await cache.get_active_profile(mock_conn, sensor_id)

        assert profile is None
        assert sensor_id in cache._cache
        assert cache._cache[sensor_id][0] is None

    @pytest.mark.asyncio
    async def test_none_cache_entry_prevents_db_calls(self):
        """Test cached None prevents unnecessary DB calls."""
        cache = ProfileCache(ttl_seconds=60.0)
        sensor_id = uuid4()

        # Pre-populate with None
        cache._cache[sensor_id] = (None, time.monotonic() + 60.0)

        mock_conn = AsyncMock()

        profile = await cache.get_active_profile(mock_conn, sensor_id)

        assert profile is None
        mock_conn.fetchrow.assert_not_called()

    @pytest.mark.asyncio
    async def test_json_string_payload_is_parsed(self):
        """Test JSON string payload is parsed to dict."""
        cache = ProfileCache(ttl_seconds=60.0)
        sensor_id = uuid4()

        mock_conn = AsyncMock()
        mock_row = {
            "id": uuid4(),
            "kind": "polynomial",
            "payload": json.dumps({"coefficients": [1.0, 2.0]}),
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        profile = await cache.get_active_profile(mock_conn, sensor_id)

        assert profile is not None
        assert profile.payload == {"coefficients": [1.0, 2.0]}
        assert isinstance(profile.payload, dict)

    @pytest.mark.asyncio
    async def test_dict_payload_is_unchanged(self):
        """Test dict payload is not double-parsed."""
        cache = ProfileCache(ttl_seconds=60.0)
        sensor_id = uuid4()

        mock_conn = AsyncMock()
        mock_row = {
            "id": uuid4(),
            "kind": "polynomial",
            "payload": {"coefficients": [1.0, 2.0]},
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        profile = await cache.get_active_profile(mock_conn, sensor_id)

        assert profile is not None
        assert profile.payload == {"coefficients": [1.0, 2.0]}

    @pytest.mark.asyncio
    async def test_different_sensors_cached_separately(self):
        """Test different sensors are cached separately."""
        cache = ProfileCache(ttl_seconds=60.0)

        sensor_id1 = uuid4()
        sensor_id2 = uuid4()

        mock_conn = AsyncMock()

        async def fetchrow_side_effect(query, sensor_id):
            if sensor_id == sensor_id1:
                return {"id": uuid4(), "kind": "type1", "payload": {"data": 1}}
            else:
                return {"id": uuid4(), "kind": "type2", "payload": {"data": 2}}

        mock_conn.fetchrow = AsyncMock(side_effect=fetchrow_side_effect)

        profile1 = await cache.get_active_profile(mock_conn, sensor_id1)
        profile2 = await cache.get_active_profile(mock_conn, sensor_id2)

        assert profile1.kind == "type1"
        assert profile2.kind == "type2"
        assert len(cache._cache) == 2


class TestProfileCacheInvalidate:
    """Tests for ProfileCache.invalidate method."""

    def test_invalidate_removes_from_cache(self):
        """Test invalidate removes entry from cache."""
        cache = ProfileCache(ttl_seconds=60.0)
        sensor_id = uuid4()

        # Pre-populate
        profile = CachedProfile(
            profile_id=uuid4(),
            kind="linear",
            payload={"a": 1.0},
        )
        cache._cache[sensor_id] = (profile, time.monotonic() + 60.0)

        assert sensor_id in cache._cache

        cache.invalidate(sensor_id)

        assert sensor_id not in cache._cache

    def test_invalidate_nonexistent_key(self):
        """Test invalidate on nonexistent key doesn't raise."""
        cache = ProfileCache(ttl_seconds=60.0)
        sensor_id = uuid4()

        # Should not raise
        cache.invalidate(sensor_id)

    def test_invalidate_returns_none(self):
        """Test invalidate returns None."""
        cache = ProfileCache(ttl_seconds=60.0)
        sensor_id = uuid4()

        result = cache.invalidate(sensor_id)
        assert result is None


class TestProfileCacheTTL:
    """Tests for ProfileCache TTL behavior."""

    @pytest.mark.asyncio
    async def test_entry_expires_after_ttl(self):
        """Test cache entry expires after TTL."""
        cache = ProfileCache(ttl_seconds=0.1)  # 100ms TTL
        sensor_id = uuid4()

        mock_conn = AsyncMock()
        mock_row = {"id": uuid4(), "kind": "v1", "payload": {"version": 1}}
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        # Load into cache
        profile1 = await cache.get_active_profile(mock_conn, sensor_id)
        assert profile1.kind == "v1"

        # Wait for TTL to expire
        await asyncio.sleep(0.15)

        # Load new version
        mock_row2 = {"id": uuid4(), "kind": "v2", "payload": {"version": 2}}
        mock_conn.fetchrow = AsyncMock(return_value=mock_row2)

        profile2 = await cache.get_active_profile(mock_conn, sensor_id)
        assert profile2.kind == "v2"

    @pytest.mark.asyncio
    async def test_entry_valid_within_ttl(self):
        """Test cache entry is valid within TTL."""
        cache = ProfileCache(ttl_seconds=1.0)  # 1 second TTL
        sensor_id = uuid4()

        mock_conn = AsyncMock()
        mock_row = {"id": uuid4(), "kind": "v1", "payload": {"version": 1}}
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        # Load into cache
        profile1 = await cache.get_active_profile(mock_conn, sensor_id)

        # Wait but within TTL
        await asyncio.sleep(0.1)

        # Should return cached value (no DB call)
        profile2 = await cache.get_active_profile(mock_conn, sensor_id)

        assert profile1 == profile2
        mock_conn.fetchrow.assert_called_once()

    @pytest.mark.asyncio
    async def test_zero_ttl_always_loads_from_db(self):
        """Test zero TTL always loads from database."""
        cache = ProfileCache(ttl_seconds=0.0)
        sensor_id = uuid4()

        mock_conn = AsyncMock()
        mock_row = {"id": uuid4(), "kind": "v1", "payload": {"version": 1}}
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        # Load multiple times
        await cache.get_active_profile(mock_conn, sensor_id)
        await cache.get_active_profile(mock_conn, sensor_id)
        await cache.get_active_profile(mock_conn, sensor_id)

        # Should call DB each time
        assert mock_conn.fetchrow.call_count == 3


class TestProfileCacheEdgeCases:
    """Tests for ProfileCache edge cases."""

    @pytest.mark.asyncio
    async def test_invalid_json_payload(self):
        """Test invalid JSON payload raises."""
        cache = ProfileCache(ttl_seconds=60.0)
        sensor_id = uuid4()

        mock_conn = AsyncMock()
        mock_row = {
            "id": uuid4(),
            "kind": "polynomial",
            "payload": "not-valid-json{",
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        with pytest.raises(json.JSONDecodeError):
            await cache.get_active_profile(mock_conn, sensor_id)

    @pytest.mark.asyncio
    async def test_uuid_conversion(self):
        """Test UUID is properly converted from database."""
        cache = ProfileCache(ttl_seconds=60.0)
        sensor_id = uuid4()

        profile_id = uuid4()
        mock_conn = AsyncMock()
        mock_row = {
            "id": str(profile_id),  # DB returns string
            "kind": "linear",
            "payload": {"a": 1.0},
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        profile = await cache.get_active_profile(mock_conn, sensor_id)

        assert profile.profile_id == profile_id
        assert isinstance(profile.profile_id, type(profile_id))

    @pytest.mark.asyncio
    async def test_empty_payload(self):
        """Test empty payload dict."""
        cache = ProfileCache(ttl_seconds=60.0)
        sensor_id = uuid4()

        mock_conn = AsyncMock()
        mock_row = {
            "id": uuid4(),
            "kind": "linear",
            "payload": {},
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        profile = await cache.get_active_profile(mock_conn, sensor_id)

        assert profile is not None
        assert profile.payload == {}

    @pytest.mark.asyncio
    async def test_null_payload(self):
        """Test NULL payload from database."""
        cache = ProfileCache(ttl_seconds=60.0)
        sensor_id = uuid4()

        mock_conn = AsyncMock()
        mock_row = {
            "id": uuid4(),
            "kind": "linear",
            "payload": None,
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        profile = await cache.get_active_profile(mock_conn, sensor_id)

        assert profile is not None
        assert profile.payload is None


class TestProfileCacheSingleton:
    """Tests for module-level profile_cache singleton."""

    def test_singleton_exists(self):
        """Test profile_cache singleton exists."""
        assert profile_cache is not None

    def test_singleton_is_profile_cache_instance(self):
        """Test profile_cache is ProfileCache instance."""
        assert isinstance(profile_cache, ProfileCache)

    def test_singleton_uses_settings_ttl(self):
        """Test profile_cache uses settings TTL."""
        expected_ttl = settings.conversion_profile_cache_ttl_seconds
        assert profile_cache._ttl == expected_ttl


class TestProfileCacheConcurrency:
    """Tests for ProfileCache concurrent access."""

    @pytest.mark.asyncio
    async def test_concurrent_cache_access(self):
        """Test concurrent cache access is safe."""
        cache = ProfileCache(ttl_seconds=60.0)
        sensor_ids = [uuid4() for _ in range(10)]

        mock_conn = AsyncMock()

        async def fetchrow_side_effect(query, sensor_id):
            return {
                "id": sensor_id,
                "kind": f"kind-{sensor_id}",
                "payload": {"sensor": str(sensor_id)},
            }

        mock_conn.fetchrow = AsyncMock(side_effect=fetchrow_side_effect)

        async def get_profile(sensor_id):
            return await cache.get_active_profile(mock_conn, sensor_id)

        # Concurrent access
        results = await asyncio.gather(*[get_profile(sid) for sid in sensor_ids])

        # All should succeed
        assert len(results) == 10
        for i, profile in enumerate(results):
            assert profile is not None
            assert profile.kind == f"kind-{sensor_ids[i]}"

    @pytest.mark.asyncio
    async def test_concurrent_same_sensor(self):
        """Test concurrent access to same sensor."""
        cache = ProfileCache(ttl_seconds=60.0)
        sensor_id = uuid4()

        mock_conn = AsyncMock()
        mock_row = {"id": uuid4(), "kind": "linear", "payload": {"a": 1.0}}
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        async def get_profile():
            return await cache.get_active_profile(mock_conn, sensor_id)

        # First call loads from DB
        profile1 = await get_profile()

        # Concurrent calls should use cache
        profiles = await asyncio.gather(*[get_profile() for _ in range(5)])

        # All should be same
        for profile in profiles:
            assert profile == profile1

        # DB should only be called once
        assert mock_conn.fetchrow.call_count == 1


class TestProfileCacheIntegration:
    """Integration tests for ProfileCache."""

    @pytest.mark.asyncio
    async def test_full_workflow(self):
        """Test complete cache workflow."""
        cache = ProfileCache(ttl_seconds=0.2)
        sensor_id = uuid4()

        mock_conn = AsyncMock()

        # First access - cache miss
        mock_row1 = {
            "id": uuid4(),
            "kind": "linear",
            "payload": {"a": 1.0, "b": 2.0},
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row1)

        profile1 = await cache.get_active_profile(mock_conn, sensor_id)
        assert profile1.kind == "linear"
        assert mock_conn.fetchrow.call_count == 1

        # Second access - cache hit
        profile2 = await cache.get_active_profile(mock_conn, sensor_id)
        assert profile2 == profile1
        assert mock_conn.fetchrow.call_count == 1  # No new DB call

        # Invalidate
        cache.invalidate(sensor_id)
        assert sensor_id not in cache._cache

        # Third access - cache miss after invalidate
        mock_row2 = {
            "id": uuid4(),
            "kind": "polynomial",
            "payload": {"coefficients": [1.0, 2.0]},
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row2)

        profile3 = await cache.get_active_profile(mock_conn, sensor_id)
        assert profile3.kind == "polynomial"
        assert mock_conn.fetchrow.call_count == 2
        assert profile3 != profile1
