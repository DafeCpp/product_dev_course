"""Unit tests for telemetry_ingest_service.services.telemetry module."""
from __future__ import annotations

import asyncio
import json
from datetime import datetime, timezone
from unittest.mock import AsyncMock, MagicMock, patch
from uuid import uuid4

import pytest
from asyncpg.exceptions import PostgresError

from telemetry_ingest_service.core.exceptions import (
    NotFoundError,
    ScopeMismatchError,
    UnauthorizedError,
)
from telemetry_ingest_service.domain.dto import TelemetryIngestDTO, TelemetryReadingDTO
from telemetry_ingest_service.services.telemetry import (
    TelemetryIngestService,
    _SensorAuth,
    _items_to_dicts,
    hash_sensor_token,
    items_from_dicts,
)


# ---------------------------------------------------------------------------
# Helper function tests
# ---------------------------------------------------------------------------

class TestHashSensorToken:
    """Tests for hash_sensor_token function."""

    def test_returns_bytes(self):
        """Test hash_sensor_token returns bytes."""
        result = hash_sensor_token("test-token")
        assert isinstance(result, bytes)

    def test_same_token_same_hash(self):
        """Test same token produces same hash."""
        hash1 = hash_sensor_token("test-token")
        hash2 = hash_sensor_token("test-token")
        assert hash1 == hash2

    def test_different_tokens_different_hashes(self):
        """Test different tokens produce different hashes."""
        hash1 = hash_sensor_token("token-1")
        hash2 = hash_sensor_token("token-2")
        assert hash1 != hash2

    def test_sha256_length(self):
        """Test hash is SHA-256 (32 bytes)."""
        result = hash_sensor_token("test-token")
        assert len(result) == 32

    def test_empty_token(self):
        """Test hash with empty token."""
        result = hash_sensor_token("")
        assert len(result) == 32

    def test_unicode_token(self):
        """Test hash with unicode token."""
        result = hash_sensor_token("пароль")
        assert len(result) == 32


class TestItemsToDicts:
    """Tests for _items_to_dicts function."""

    def test_converts_tuple_to_dict(self):
        """Test conversion of single tuple to dict."""
        project_id = uuid4()
        sensor_id = uuid4()
        run_id = uuid4()
        capture_id = uuid4()
        ts = datetime.now(timezone.utc)
        conv_prof = uuid4()

        items = [(
            project_id,
            sensor_id,
            run_id,
            capture_id,
            ts,
            1.0,
            2.0,
            '{"key": "value"}',
            "converted",
            conv_prof,
        )]

        result = _items_to_dicts(items)

        assert len(result) == 1
        assert result[0]["project_id"] == str(project_id)
        assert result[0]["sensor_id"] == str(sensor_id)
        assert result[0]["run_id"] == str(run_id)
        assert result[0]["capture_session_id"] == str(capture_id)
        assert result[0]["timestamp"] == ts.isoformat()
        assert result[0]["raw_value"] == 1.0
        assert result[0]["physical_value"] == 2.0
        assert result[0]["meta"] == '{"key": "value"}'
        assert result[0]["conversion_status"] == "converted"
        assert result[0]["conversion_profile_id"] == str(conv_prof)

    def test_handles_none_run_id(self):
        """Test handles None run_id."""
        items = [(
            uuid4(), uuid4(), None, None,
            datetime.now(timezone.utc), 1.0, 2.0, '{}', "raw_only", None,
        )]

        result = _items_to_dicts(items)

        assert result[0]["run_id"] is None
        assert result[0]["capture_session_id"] is None
        assert result[0]["conversion_profile_id"] is None

    def test_multiple_items(self):
        """Test conversion of multiple items."""
        items = [
            (uuid4(), uuid4(), None, None, datetime.now(timezone.utc), 1.0, 2.0, '{}', "raw_only", None),
            (uuid4(), uuid4(), None, None, datetime.now(timezone.utc), 3.0, 4.0, '{}', "raw_only", None),
        ]

        result = _items_to_dicts(items)

        assert len(result) == 2

    def test_preserves_meta_json_string(self):
        """Test meta JSON string is preserved as-is."""
        meta_json = '{"nested": {"key": "value"}}'
        items = [(
            uuid4(), uuid4(), None, None,
            datetime.now(timezone.utc), 1.0, 2.0, meta_json, "raw_only", None,
        )]

        result = _items_to_dicts(items)

        assert result[0]["meta"] == meta_json


class TestItemsFromDicts:
    """Tests for items_from_dicts function."""

    def test_converts_dict_to_tuple(self):
        """Test conversion of single dict to tuple."""
        project_id = uuid4()
        sensor_id = uuid4()
        run_id = uuid4()
        capture_id = uuid4()
        ts = datetime.now(timezone.utc)
        conv_prof = uuid4()

        dicts = [{
            "project_id": str(project_id),
            "sensor_id": str(sensor_id),
            "run_id": str(run_id),
            "capture_session_id": str(capture_id),
            "timestamp": ts.isoformat(),
            "raw_value": 1.0,
            "physical_value": 2.0,
            "meta": '{"key": "value"}',
            "conversion_status": "converted",
            "conversion_profile_id": str(conv_prof),
        }]

        result = items_from_dicts(dicts)

        assert len(result) == 1
        item = result[0]
        assert item[0] == project_id
        assert item[1] == sensor_id
        assert item[2] == run_id
        assert item[3] == capture_id
        assert item[4] == ts
        assert item[5] == 1.0
        assert item[6] == 2.0
        assert item[7] == '{"key": "value"}'
        assert item[8] == "converted"
        assert item[9] == conv_prof

    def test_handles_none_run_id(self):
        """Test handles None run_id."""
        dicts = [{
            "project_id": str(uuid4()),
            "sensor_id": str(uuid4()),
            "run_id": None,
            "capture_session_id": None,
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "raw_value": 1.0,
            "physical_value": 2.0,
            "meta": '{}',
            "conversion_status": "raw_only",
            "conversion_profile_id": None,
        }]

        result = items_from_dicts(dicts)

        assert result[0][2] is None
        assert result[0][3] is None
        assert result[0][9] is None

    def test_multiple_dicts(self):
        """Test conversion of multiple dicts."""
        dicts = [
            {
                "project_id": str(uuid4()),
                "sensor_id": str(uuid4()),
                "run_id": None,
                "capture_session_id": None,
                "timestamp": datetime.now(timezone.utc).isoformat(),
                "raw_value": 1.0,
                "physical_value": 2.0,
                "meta": '{}',
                "conversion_status": "raw_only",
                "conversion_profile_id": None,
            },
            {
                "project_id": str(uuid4()),
                "sensor_id": str(uuid4()),
                "run_id": None,
                "capture_session_id": None,
                "timestamp": datetime.now(timezone.utc).isoformat(),
                "raw_value": 3.0,
                "physical_value": 4.0,
                "meta": '{}',
                "conversion_status": "raw_only",
                "conversion_profile_id": None,
            },
        ]

        result = items_from_dicts(dicts)

        assert len(result) == 2

    def test_roundtrip(self):
        """Test roundtrip conversion."""
        original_items = [
            (uuid4(), uuid4(), None, None, datetime.now(timezone.utc), 1.0, 2.0, '{}', "raw_only", None),
        ]

        dicts = _items_to_dicts(original_items)
        restored = items_from_dicts(dicts)

        assert len(restored) == len(original_items)
        assert restored[0][0] == original_items[0][0]  # project_id
        assert restored[0][1] == original_items[0][1]  # sensor_id
        assert restored[0][5] == original_items[0][5]  # raw_value


# ---------------------------------------------------------------------------
# TelemetryIngestService tests
# ---------------------------------------------------------------------------

class TestTelemetryIngestServiceAuth:
    """Tests for TelemetryIngestService authentication."""

    @pytest.mark.asyncio
    async def test_authenticate_sensor_success(self):
        """Test _authenticate_sensor returns SensorAuth."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        sensor_id = uuid4()
        project_id = uuid4()
        token_hash = b"fake_hash"

        mock_conn.fetchrow = AsyncMock(return_value={"project_id": project_id, "run_id": uuid4(), "capture_session_id": uuid4(), "status": "running", "payload": {}, "kind": "default"})

        result = await service._authenticate_sensor(mock_conn, sensor_id, token_hash)

        assert isinstance(result, _SensorAuth)
        assert result.project_id == project_id
        mock_conn.fetchrow.assert_called_once()

    @pytest.mark.asyncio
    async def test_authenticate_sensor_not_found(self):
        """Test _authenticate_sensor raises UnauthorizedError."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        sensor_id = uuid4()
        token_hash = b"fake_hash"

        mock_conn.fetchrow = AsyncMock(return_value=None)

        with pytest.raises(UnauthorizedError, match="Invalid sensor credentials"):
            await service._authenticate_sensor(mock_conn, sensor_id, token_hash)


class TestTelemetryIngestServiceScopeResolution:
    """Tests for TelemetryIngestService scope resolution."""

    @pytest.mark.asyncio
    async def test_ensure_run_scope_success(self):
        """Test _ensure_run_scope returns run_id."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        project_id = uuid4()
        run_id = uuid4()

        mock_conn.fetchrow = AsyncMock(return_value={"id": run_id, "status": "running", "payload": {}, "kind": "default"})

        result = await service._ensure_run_scope(mock_conn, project_id, run_id)

        assert result == run_id

    @pytest.mark.asyncio
    async def test_ensure_run_scope_none_run_id(self):
        """Test _ensure_run_scope returns None for None run_id."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        project_id = uuid4()

        result = await service._ensure_run_scope(mock_conn, project_id, None)

        assert result is None

    @pytest.mark.asyncio
    async def test_ensure_run_scope_not_found(self):
        """Test _ensure_run_scope raises NotFoundError."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        project_id = uuid4()
        run_id = uuid4()

        mock_conn.fetchrow = AsyncMock(return_value=None)

        with pytest.raises(NotFoundError, match="Run not found"):
            await service._ensure_run_scope(mock_conn, project_id, run_id)

    @pytest.mark.asyncio
    async def test_ensure_run_scope_archived(self):
        """Test _ensure_run_scope raises ScopeMismatchError for archived run."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        project_id = uuid4()
        run_id = uuid4()

        mock_conn.fetchrow = AsyncMock(return_value={"id": run_id, "status": "archived"})

        with pytest.raises(ScopeMismatchError, match="archived"):
            await service._ensure_run_scope(mock_conn, project_id, run_id)

    @pytest.mark.asyncio
    async def test_ensure_capture_scope_success(self):
        """Test _ensure_capture_scope returns run_id."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        project_id = uuid4()
        capture_id = uuid4()
        run_id = uuid4()

        mock_conn.fetchrow = AsyncMock(return_value={"run_id": run_id, "status": "running", "archived": False, "payload": {}, "kind": "default"})

        result = await service._ensure_capture_scope(mock_conn, project_id, capture_id)

        assert result == run_id

    @pytest.mark.asyncio
    async def test_ensure_capture_scope_none(self):
        """Test _ensure_capture_scope returns None for None capture_id."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        project_id = uuid4()

        result = await service._ensure_capture_scope(mock_conn, project_id, None)

        assert result is None

    @pytest.mark.asyncio
    async def test_ensure_capture_scope_not_found(self):
        """Test _ensure_capture_scope raises NotFoundError."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        project_id = uuid4()
        capture_id = uuid4()

        mock_conn.fetchrow = AsyncMock(return_value=None)

        with pytest.raises(NotFoundError, match="Capture session not found"):
            await service._ensure_capture_scope(mock_conn, project_id, capture_id)

    @pytest.mark.asyncio
    async def test_ensure_capture_scope_archived(self):
        """Test _ensure_capture_scope raises ScopeMismatchError for archived session."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        project_id = uuid4()
        capture_id = uuid4()

        mock_conn.fetchrow = AsyncMock(return_value={"run_id": uuid4(), "status": "archived", "archived": True})

        with pytest.raises(ScopeMismatchError, match="archived"):
            await service._ensure_capture_scope(mock_conn, project_id, capture_id)

    @pytest.mark.asyncio
    async def test_get_capture_status(self):
        """Test _get_capture_status returns status."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        project_id = uuid4()
        capture_id = uuid4()

        mock_conn.fetchrow = AsyncMock(return_value={"status": "running", "payload": {}, "kind": "default"})

        result = await service._get_capture_status(mock_conn, project_id, capture_id)

        assert result == "running"

    @pytest.mark.asyncio
    async def test_get_capture_status_not_found(self):
        """Test _get_capture_status returns None."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        mock_conn.fetchrow = AsyncMock(return_value=None)

        result = await service._get_capture_status(mock_conn, uuid4(), uuid4())

        assert result is None

    @pytest.mark.asyncio
    async def test_find_active_capture_session(self):
        """Test _find_active_capture_session returns session."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        project_id = uuid4()
        run_id = uuid4()
        capture_id = uuid4()

        mock_conn.fetchrow = AsyncMock(return_value={"id": capture_id, "status": "running", "payload": {}, "kind": "default"})

        result = await service._find_active_capture_session(mock_conn, project_id, run_id)

        assert result == (capture_id, "running")

    @pytest.mark.asyncio
    async def test_find_active_capture_session_none(self):
        """Test _find_active_capture_session returns None."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        mock_conn.fetchrow = AsyncMock(return_value=None)

        result = await service._find_active_capture_session(mock_conn, uuid4(), uuid4())

        assert result is None

    @pytest.mark.asyncio
    async def test_find_active_capture_session_in_project(self):
        """Test _find_active_capture_session_in_project returns session."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        project_id = uuid4()
        run_id = uuid4()
        capture_id = uuid4()

        mock_conn.fetchrow = AsyncMock(return_value={
            "run_id": run_id,
            "capture_session_id": capture_id,
            "status": "running",
        })

        result = await service._find_active_capture_session_in_project(mock_conn, project_id)

        assert result == (run_id, capture_id, "running")


class TestTelemetryIngestServicePrepareItems:
    """Tests for TelemetryIngestService _prepare_items method."""

    @pytest.mark.asyncio
    async def test_prepare_items_basic(self):
        """Test _prepare_items creates correct items."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        project_id = uuid4()
        sensor_id = uuid4()

        payload = TelemetryIngestDTO(
            sensor_id=sensor_id,
            run_id=None,
            capture_session_id=None,
            meta={},
            readings=[
                TelemetryReadingDTO(
                    timestamp=datetime.now(timezone.utc),
                    raw_value=1.0,
                    physical_value=None,
                    meta={},
                ),
            ],
        )

        # Mock profile cache
        with patch("telemetry_ingest_service.services.telemetry.profile_cache") as mock_cache:
            mock_cache.get_active_profile = AsyncMock(return_value=None)

            items, last_ts = await service._prepare_items(
                mock_conn, project_id, payload,
                run_id=None,
                capture_session_id=None,
                capture_status=None,
                system_meta={},
            )

            assert len(items) == 1
            assert items[0][0] == project_id
            assert items[0][1] == sensor_id
            assert items[0][5] == 1.0  # raw_value
            assert last_ts is not None

    @pytest.mark.asyncio
    async def test_prepare_items_with_conversion(self):
        """Test _prepare_items applies conversion."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        project_id = uuid4()
        sensor_id = uuid4()

        payload = TelemetryIngestDTO(
            sensor_id=sensor_id,
            run_id=None,
            capture_session_id=None,
            meta={},
            readings=[
                TelemetryReadingDTO(
                    timestamp=datetime.now(timezone.utc),
                    raw_value=10.0,
                    physical_value=None,
                    meta={},
                ),
            ],
        )

        # Mock profile cache with linear conversion (a=2, b=5)
        mock_profile = MagicMock()
        mock_profile.kind = "linear"
        mock_profile.payload = {"a": 2.0, "b": 5.0}
        mock_profile.profile_id = uuid4()

        with patch("telemetry_ingest_service.services.telemetry.profile_cache") as mock_cache, \
             patch("telemetry_ingest_service.services.telemetry.apply_conversion") as mock_convert:

            mock_cache.get_active_profile = AsyncMock(return_value=mock_profile)
            mock_convert.return_value = 25.0  # 10 * 2 + 5

            items, _ = await service._prepare_items(
                mock_conn, project_id, payload,
                run_id=None,
                capture_session_id=None,
                capture_status=None,
                system_meta={},
            )

            assert items[0][6] == 25.0  # physical_value
            assert items[0][8] == "converted"  # conversion_status

    @pytest.mark.asyncio
    async def test_prepare_items_with_client_physical_value(self):
        """Test _prepare_items preserves client-provided physical value."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        project_id = uuid4()
        sensor_id = uuid4()

        payload = TelemetryIngestDTO(
            sensor_id=sensor_id,
            run_id=None,
            capture_session_id=None,
            meta={},
            readings=[
                TelemetryReadingDTO(
                    timestamp=datetime.now(timezone.utc),
                    raw_value=10.0,
                    physical_value=100.0,
                    meta={},
                ),
            ],
        )

        with patch("telemetry_ingest_service.services.telemetry.profile_cache") as mock_cache:
            mock_cache.get_active_profile = AsyncMock(return_value=None)

            items, _ = await service._prepare_items(
                mock_conn, project_id, payload,
                run_id=None,
                capture_session_id=None,
                capture_status=None,
                system_meta={},
            )

            assert items[0][6] == 100.0  # physical_value
            assert items[0][8] == "client_provided"  # conversion_status

    @pytest.mark.asyncio
    async def test_prepare_items_with_late_marker(self):
        """Test _prepare_items adds late marker for succeeded capture."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        project_id = uuid4()
        sensor_id = uuid4()

        payload = TelemetryIngestDTO(
            sensor_id=sensor_id,
            run_id=None,
            capture_session_id=None,
            meta={},
            readings=[
                TelemetryReadingDTO(
                    timestamp=datetime.now(timezone.utc),
                    raw_value=1.0,
                    physical_value=None,
                    meta={},
                ),
            ],
        )

        with patch("telemetry_ingest_service.services.telemetry.profile_cache") as mock_cache:
            mock_cache.get_active_profile = AsyncMock(return_value=None)

            items, _ = await service._prepare_items(
                mock_conn, project_id, payload,
                run_id=None,
                capture_session_id=None,
                capture_status="succeeded",  # Late data
                system_meta={},
            )

            meta = json.loads(items[0][7])
            assert meta.get("__system", {}).get("late") is True

    @pytest.mark.asyncio
    async def test_prepare_items_validates_meta_size(self):
        """Test _prepare_items validates meta size."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        project_id = uuid4()
        sensor_id = uuid4()

        # Create oversized meta
        large_meta = {"key": "x" * 100000}

        payload = TelemetryIngestDTO(
            sensor_id=sensor_id,
            run_id=None,
            capture_session_id=None,
            meta=large_meta,
            readings=[
                TelemetryReadingDTO(
                    timestamp=datetime.now(timezone.utc),
                    raw_value=1.0,
                    physical_value=None,
                    meta={},
                ),
            ],
        )

        with patch("telemetry_ingest_service.services.telemetry.settings") as mock_settings:
            mock_settings.telemetry_max_batch_meta_bytes = 1000

            with pytest.raises(ScopeMismatchError, match="too large"):
                await service._prepare_items(
                    mock_conn, project_id, payload,
                    run_id=None,
                    capture_session_id=None,
                    capture_status=None,
                    system_meta={},
                )


class TestTelemetryIngestServiceIngest:
    """Tests for TelemetryIngestService ingest method."""

    @pytest.mark.asyncio
    async def test_ingest_success(self):
        """Test ingest succeeds with valid payload."""
        service = TelemetryIngestService()

        sensor_id = uuid4()
        token = "test-token"
        token_hash = hash_sensor_token(token)

        payload = TelemetryIngestDTO(
            sensor_id=sensor_id,
            run_id=None,
            capture_session_id=None,
            meta={},
            readings=[
                TelemetryReadingDTO(
                    timestamp=datetime.now(timezone.utc),
                    raw_value=1.0,
                    physical_value=None,
                    meta={},
                ),
            ],
        )

        with patch("telemetry_ingest_service.services.telemetry.get_pool") as mock_get_pool, \
             patch("telemetry_ingest_service.services.telemetry.settings") as mock_settings:

            mock_settings.telemetry_max_batch_meta_bytes = 1024 * 1024
            mock_settings.telemetry_max_reading_meta_bytes = 1024 * 1024  # 1MB
            mock_settings.telemetry_max_reading_meta_bytes = 1024 * 1024  # 1MB
            mock_pool = MagicMock()
            mock_get_pool.return_value = mock_pool

            mock_conn = AsyncMock()
            mock_cm = MagicMock()
            mock_cm.__aenter__ = AsyncMock(return_value=mock_conn)
            mock_cm.__aexit__ = AsyncMock(return_value=None)
            mock_pool.acquire.return_value = mock_cm

            # Phase 1: auth
            mock_conn.fetchrow = AsyncMock(return_value={
                "id": uuid4(),
                "project_id": uuid4(),
                "run_id": uuid4(),
                "capture_session_id": uuid4(),
                "status": "running",
                "payload": {},
                "kind": "default"
            })

            # Phase 2: insert
            mock_conn.transaction = MagicMock()
            mock_transaction = MagicMock()
            mock_transaction.__aenter__ = AsyncMock(return_value=mock_transaction)
            mock_transaction.__aexit__ = AsyncMock(return_value=None)
            mock_conn.transaction.return_value = mock_transaction
            # execute is called once per INSERT row then once for heartbeat UPDATE
            mock_conn.execute = AsyncMock(side_effect=["INSERT 0 1", "UPDATE 1"])

            result = await service.ingest(payload, token=token)

            assert result == 1  # Number of readings actually inserted

    @pytest.mark.asyncio
    async def test_ingest_unauthorized_sensor(self):
        """Test ingest raises UnauthorizedError for invalid sensor."""
        service = TelemetryIngestService()

        sensor_id = uuid4()
        token = "invalid-token"

        payload = TelemetryIngestDTO(
            sensor_id=sensor_id,
            run_id=None,
            capture_session_id=None,
            meta={},
            readings=[
                TelemetryReadingDTO(
                    timestamp=datetime.now(timezone.utc),
                    raw_value=1.0,
                    physical_value=None,
                    meta={},
                ),
            ],
        )

        with patch("telemetry_ingest_service.services.telemetry.get_pool") as mock_get_pool:
            mock_pool = MagicMock()
            mock_get_pool.return_value = mock_pool

            mock_conn = AsyncMock()
            mock_cm = MagicMock()
            mock_cm.__aenter__ = AsyncMock(return_value=mock_conn)
            mock_cm.__aexit__ = AsyncMock(return_value=None)
            mock_pool.acquire.return_value = mock_cm
            mock_conn.fetchrow = AsyncMock(return_value=None)  # Sensor not found

            with pytest.raises(UnauthorizedError):
                await service.ingest(payload, token=token)

    @pytest.mark.asyncio
    async def test_ingest_spool_on_db_error(self):
        """Test ingest writes to spool on DB error."""
        service = TelemetryIngestService()

        sensor_id = uuid4()
        token = "test-token"

        payload = TelemetryIngestDTO(
            sensor_id=sensor_id,
            run_id=None,
            capture_session_id=None,
            meta={},
            readings=[
                TelemetryReadingDTO(
                    timestamp=datetime.now(timezone.utc),
                    raw_value=1.0,
                    physical_value=None,
                    meta={},
                ),
            ],
        )

        with patch("telemetry_ingest_service.services.telemetry.get_pool") as mock_get_pool, \
             patch("telemetry_ingest_service.services.telemetry.settings") as mock_settings, \
             patch("telemetry_ingest_service.services.telemetry.write_spool") as mock_write_spool:

            mock_settings.spool_enabled = True
            mock_settings.telemetry_max_batch_meta_bytes = 1024 * 1024
            mock_settings.telemetry_max_reading_meta_bytes = 1024 * 1024
            mock_pool = MagicMock()
            mock_get_pool.return_value = mock_pool

            mock_conn = AsyncMock()
            mock_cm = MagicMock()
            mock_cm.__aenter__ = AsyncMock(return_value=mock_conn)
            mock_cm.__aexit__ = AsyncMock(return_value=None)
            mock_pool.acquire.return_value = mock_cm

            # Phase 1: auth succeeds
            mock_conn.fetchrow = AsyncMock(return_value={"id": uuid4(), "project_id": uuid4(), "run_id": uuid4(), "capture_session_id": uuid4(), "status": "running", "payload": {}, "kind": "default"})

            # Phase 2: insert fails
            mock_conn.transaction = MagicMock()
            mock_transaction = MagicMock()
            mock_transaction.__aenter__ = AsyncMock(return_value=mock_transaction)
            mock_transaction.__aexit__ = AsyncMock(return_value=None)
            mock_conn.transaction.return_value = mock_transaction
            mock_conn.execute = AsyncMock(side_effect=PostgresError("DB error"))

            result = await service.ingest(payload, token=token)

            # Should return optimistic 202
            assert result == 1
            mock_write_spool.assert_called_once()

    @pytest.mark.asyncio
    async def test_ingest_raises_on_db_error_without_spool(self):
        """Test ingest raises on DB error when spool disabled."""
        service = TelemetryIngestService()

        sensor_id = uuid4()
        token = "test-token"

        payload = TelemetryIngestDTO(
            sensor_id=sensor_id,
            run_id=None,
            capture_session_id=None,
            meta={},
            readings=[
                TelemetryReadingDTO(
                    timestamp=datetime.now(timezone.utc),
                    raw_value=1.0,
                    physical_value=None,
                    meta={},
                ),
            ],
        )

        with patch("telemetry_ingest_service.services.telemetry.get_pool") as mock_get_pool, \
             patch("telemetry_ingest_service.services.telemetry.settings") as mock_settings:

            mock_settings.spool_enabled = False
            mock_settings.telemetry_max_batch_meta_bytes = 1024 * 1024
            mock_settings.telemetry_max_reading_meta_bytes = 1024 * 1024
            mock_pool = MagicMock()
            mock_get_pool.return_value = mock_pool

            mock_conn = AsyncMock()
            mock_cm = MagicMock()
            mock_cm.__aenter__ = AsyncMock(return_value=mock_conn)
            mock_cm.__aexit__ = AsyncMock(return_value=None)
            mock_pool.acquire.return_value = mock_cm

            # Phase 1: auth succeeds
            mock_conn.fetchrow = AsyncMock(return_value={"id": uuid4(), "project_id": uuid4(), "run_id": uuid4(), "capture_session_id": uuid4(), "status": "running", "payload": {}, "kind": "default"})

            # Phase 2: insert fails
            mock_conn.transaction = MagicMock()
            mock_transaction = MagicMock()
            mock_transaction.__aenter__ = AsyncMock(return_value=mock_transaction)
            mock_transaction.__aexit__ = AsyncMock(return_value=None)
            mock_conn.transaction.return_value = mock_transaction
            mock_conn.execute = AsyncMock(side_effect=PostgresError("DB error"))

            with pytest.raises(PostgresError):
                await service.ingest(payload, token=token)


class TestTelemetryIngestServiceFlushSpool:
    """Tests for TelemetryIngestService _flush_spool_record method."""

    @pytest.mark.asyncio
    async def test_flush_spool_record(self):
        """Test _flush_spool_record replays spooled items."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        from telemetry_ingest_service.services.spool import SpoolRecord

        record = SpoolRecord(
            sensor_id=uuid4(),
            items=[
                {
                    "project_id": str(uuid4()),
                    "sensor_id": str(uuid4()),
                    "run_id": None,
                    "capture_session_id": None,
                    "timestamp": datetime.now(timezone.utc).isoformat(),
                    "raw_value": 1.0,
                    "physical_value": 2.0,
                    "meta": '{}',
                    "conversion_status": "raw_only",
                    "conversion_profile_id": None,
                },
            ],
            last_reading_ts=datetime.now(timezone.utc).isoformat(),
        )

        # execute is called once per INSERT row + once for heartbeat UPDATE
        mock_conn.execute = AsyncMock(side_effect=["INSERT 0 1", "UPDATE 1"])

        await service._flush_spool_record(mock_conn, record)

        # 2 execute calls: 1 INSERT + 1 heartbeat UPDATE
        assert mock_conn.execute.call_count == 2


class TestTelemetryIngestServiceHeartbeat:
    """Tests for TelemetryIngestService heartbeat methods."""

    @pytest.mark.asyncio
    async def test_update_sensor_heartbeat_ts(self):
        """Test _update_sensor_heartbeat_ts updates sensor."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        sensor_id = uuid4()
        last_ts = datetime.now(timezone.utc)

        mock_conn.execute = AsyncMock()

        await service._update_sensor_heartbeat_ts(mock_conn, sensor_id, last_ts)

        mock_conn.execute.assert_called_once()

    @pytest.mark.asyncio
    async def test_update_sensor_heartbeat(self):
        """Test _update_sensor_heartbeat updates sensor."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        sensor_id = uuid4()

        readings = [
            TelemetryReadingDTO(
                timestamp=datetime.now(timezone.utc),
                raw_value=1.0,
                physical_value=None,
                meta={},
            ),
            TelemetryReadingDTO(
                timestamp=datetime.now(timezone.utc),
                raw_value=2.0,
                physical_value=None,
                meta={},
            ),
        ]

        mock_conn.execute = AsyncMock()

        await service._update_sensor_heartbeat(mock_conn, sensor_id, readings)

        mock_conn.execute.assert_called_once()
