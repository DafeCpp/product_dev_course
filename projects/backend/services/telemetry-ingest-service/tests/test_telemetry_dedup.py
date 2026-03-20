"""Unit tests for telemetry deduplication logic.

Covered cases:
- test_duplicate_readings_are_ignored
- test_bulk_insert_returns_actual_inserted_count
- test_different_signals_same_timestamp_are_not_duplicates
"""
from __future__ import annotations

import json
from datetime import datetime, timezone
from unittest.mock import AsyncMock, patch
from uuid import uuid4

import pytest

from telemetry_ingest_service.domain.dto import TelemetryIngestDTO, TelemetryReadingDTO
from telemetry_ingest_service.services.telemetry import TelemetryIngestService


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_reading(
    ts: datetime,
    raw_value: float = 1.0,
    signal: str | None = "accel_x",
) -> TelemetryReadingDTO:
    meta: dict = {}
    if signal is not None:
        meta["signal"] = signal
    return TelemetryReadingDTO(
        timestamp=ts,
        raw_value=raw_value,
        physical_value=None,
        meta=meta,
    )


def _make_payload(
    sensor_id=None,
    readings: list[TelemetryReadingDTO] | None = None,
) -> TelemetryIngestDTO:
    if sensor_id is None:
        sensor_id = uuid4()
    if readings is None:
        ts = datetime.now(timezone.utc)
        readings = [_make_reading(ts)]
    return TelemetryIngestDTO(
        sensor_id=sensor_id,
        run_id=None,
        capture_session_id=None,
        meta={},
        readings=readings,
    )


# ---------------------------------------------------------------------------
# _do_insert unit tests
# ---------------------------------------------------------------------------

class TestDoInsert:
    """Tests for TelemetryIngestService._do_insert."""

    @pytest.mark.asyncio
    async def test_bulk_insert_returns_actual_inserted_count(self):
        """_do_insert returns the count of rows accepted by the DB.

        When the DB returns 'INSERT 0 1' for the first row and 'INSERT 0 0'
        for the second (duplicate skipped), _do_insert must return 1, not 2.
        """
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        project_id = uuid4()
        sensor_id = uuid4()
        ts = datetime.now(timezone.utc)

        items = [
            (project_id, sensor_id, None, None, ts, 1.0, None, '{"signal":"rpm"}', "raw_only", None),
            (project_id, sensor_id, None, None, ts, 1.0, None, '{"signal":"rpm"}', "raw_only", None),
        ]

        # First row inserted, second is a duplicate — DO NOTHING returns count 0.
        mock_conn.execute = AsyncMock(side_effect=["INSERT 0 1", "INSERT 0 0"])

        result = await service._do_insert(mock_conn, items)

        assert result == 1
        assert mock_conn.execute.call_count == 2

    @pytest.mark.asyncio
    async def test_all_rows_inserted_when_no_duplicates(self):
        """_do_insert returns len(items) when all rows are unique."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        project_id = uuid4()
        sensor_id = uuid4()
        ts1 = datetime(2024, 1, 1, 0, 0, 0, tzinfo=timezone.utc)
        ts2 = datetime(2024, 1, 1, 0, 0, 1, tzinfo=timezone.utc)

        items = [
            (project_id, sensor_id, None, None, ts1, 1.0, None, '{"signal":"rpm"}', "raw_only", None),
            (project_id, sensor_id, None, None, ts2, 2.0, None, '{"signal":"rpm"}', "raw_only", None),
        ]

        mock_conn.execute = AsyncMock(side_effect=["INSERT 0 1", "INSERT 0 1"])

        result = await service._do_insert(mock_conn, items)

        assert result == 2

    @pytest.mark.asyncio
    async def test_all_rows_duplicates_returns_zero(self):
        """_do_insert returns 0 when every row is a duplicate."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()

        project_id = uuid4()
        sensor_id = uuid4()
        ts = datetime.now(timezone.utc)

        items = [
            (project_id, sensor_id, None, None, ts, 1.0, None, '{"signal":"rpm"}', "raw_only", None),
            (project_id, sensor_id, None, None, ts, 1.0, None, '{"signal":"rpm"}', "raw_only", None),
            (project_id, sensor_id, None, None, ts, 1.0, None, '{"signal":"rpm"}', "raw_only", None),
        ]

        mock_conn.execute = AsyncMock(return_value="INSERT 0 0")

        result = await service._do_insert(mock_conn, items)

        assert result == 0

    @pytest.mark.asyncio
    async def test_empty_items_returns_zero(self):
        """_do_insert returns 0 for an empty items list without any DB call."""
        service = TelemetryIngestService()
        mock_conn = AsyncMock()
        mock_conn.execute = AsyncMock()

        result = await service._do_insert(mock_conn, [])

        assert result == 0
        mock_conn.execute.assert_not_called()


# ---------------------------------------------------------------------------
# Integration-style tests via ingest() with mocked DB
# ---------------------------------------------------------------------------

class TestDedupViaIngest:
    """Test dedup behaviour end-to-end through TelemetryIngestService.ingest()."""

    def _make_mock_pool(self, project_id=None, execute_side_effects=None):
        """Return (mock_pool, mock_conn) with a pre-configured sensor fetchrow.

        Phase 1 calls fetchrow several times:
          1. _authenticate_sensor       -> sensor row (returns project_id)
          2. _find_active_capture_session_in_project -> None (no active capture)
          3. profile_cache.get_active_profile -> handled by separate patch

        Phase 2 calls execute for each INSERT row then once for the heartbeat UPDATE.
        """
        from unittest.mock import MagicMock

        if project_id is None:
            project_id = uuid4()

        mock_conn = MagicMock()
        # First fetchrow: sensor auth row; all subsequent: None (no active captures/runs).
        mock_conn.fetchrow = AsyncMock(side_effect=[
            {"project_id": project_id, "status": "registering"},  # auth
            None,  # _find_active_capture_session_in_project
        ])

        mock_transaction = MagicMock()
        mock_transaction.__aenter__ = AsyncMock(return_value=mock_transaction)
        mock_transaction.__aexit__ = AsyncMock(return_value=None)
        mock_conn.transaction = MagicMock(return_value=mock_transaction)

        if execute_side_effects is not None:
            mock_conn.execute = AsyncMock(side_effect=execute_side_effects)
        else:
            mock_conn.execute = AsyncMock(return_value="INSERT 0 1")

        mock_cm = MagicMock()
        mock_cm.__aenter__ = AsyncMock(return_value=mock_conn)
        mock_cm.__aexit__ = AsyncMock(return_value=None)

        mock_pool = MagicMock()
        mock_pool.acquire = MagicMock(return_value=mock_cm)

        return mock_pool, mock_conn

    @pytest.mark.asyncio
    async def test_duplicate_readings_are_ignored(self):
        """Duplicate readings (same sensor, ts, signal) count as 0 inserted."""
        service = TelemetryIngestService()

        sensor_id = uuid4()
        ts = datetime(2024, 6, 1, 12, 0, 0, tzinfo=timezone.utc)

        reading = _make_reading(ts, raw_value=42.0, signal="accel_x")
        payload = _make_payload(sensor_id=sensor_id, readings=[reading, reading])

        mock_pool, mock_conn = self._make_mock_pool(
            execute_side_effects=[
                "INSERT 0 1",   # first reading — accepted
                "INSERT 0 0",   # second reading — duplicate, DO NOTHING
                "UPDATE 1",     # sensor heartbeat
            ]
        )

        with patch("telemetry_ingest_service.services.telemetry.get_pool", return_value=mock_pool), \
             patch("telemetry_ingest_service.services.telemetry.settings") as mock_settings, \
             patch("telemetry_ingest_service.services.telemetry.profile_cache") as mock_cache:

            mock_settings.spool_enabled = False
            mock_settings.telemetry_max_batch_meta_bytes = 1024 * 1024
            mock_settings.telemetry_max_reading_meta_bytes = 1024 * 1024
            mock_cache.get_active_profile = AsyncMock(return_value=None)

            accepted = await service.ingest(payload, token="sensor-token")

        # Only 1 of the 2 readings was actually inserted.
        assert accepted == 1

    @pytest.mark.asyncio
    async def test_bulk_insert_returns_actual_inserted_count(self):
        """ingest() returns the DB-level inserted count, not len(readings)."""
        service = TelemetryIngestService()

        sensor_id = uuid4()
        ts = datetime(2024, 6, 1, 12, 0, 0, tzinfo=timezone.utc)

        readings = [
            _make_reading(ts, raw_value=1.0, signal="volt"),
            _make_reading(ts, raw_value=1.0, signal="volt"),   # duplicate
            _make_reading(ts, raw_value=2.0, signal="current"),  # different signal → new row
        ]
        payload = _make_payload(sensor_id=sensor_id, readings=readings)

        mock_pool, _ = self._make_mock_pool(
            execute_side_effects=[
                "INSERT 0 1",   # volt reading — new
                "INSERT 0 0",   # volt duplicate — skipped
                "INSERT 0 1",   # current reading — new (different signal)
                "UPDATE 1",     # heartbeat
            ]
        )

        with patch("telemetry_ingest_service.services.telemetry.get_pool", return_value=mock_pool), \
             patch("telemetry_ingest_service.services.telemetry.settings") as mock_settings, \
             patch("telemetry_ingest_service.services.telemetry.profile_cache") as mock_cache:

            mock_settings.spool_enabled = False
            mock_settings.telemetry_max_batch_meta_bytes = 1024 * 1024
            mock_settings.telemetry_max_reading_meta_bytes = 1024 * 1024
            mock_cache.get_active_profile = AsyncMock(return_value=None)

            accepted = await service.ingest(payload, token="sensor-token")

        assert accepted == 2  # 3 sent, 1 duplicate → 2 inserted

    @pytest.mark.asyncio
    async def test_different_signals_same_timestamp_are_not_duplicates(self):
        """Readings with the same (sensor_id, timestamp) but different signals
        are distinct rows and must NOT be deduplicated."""
        service = TelemetryIngestService()

        sensor_id = uuid4()
        ts = datetime(2024, 6, 1, 12, 0, 0, tzinfo=timezone.utc)

        readings = [
            _make_reading(ts, raw_value=1.0, signal="accel_x"),
            _make_reading(ts, raw_value=2.0, signal="accel_y"),
            _make_reading(ts, raw_value=3.0, signal="accel_z"),
        ]
        payload = _make_payload(sensor_id=sensor_id, readings=readings)

        mock_pool, _ = self._make_mock_pool(
            execute_side_effects=[
                "INSERT 0 1",  # accel_x
                "INSERT 0 1",  # accel_y
                "INSERT 0 1",  # accel_z
                "UPDATE 1",    # heartbeat
            ]
        )

        with patch("telemetry_ingest_service.services.telemetry.get_pool", return_value=mock_pool), \
             patch("telemetry_ingest_service.services.telemetry.settings") as mock_settings, \
             patch("telemetry_ingest_service.services.telemetry.profile_cache") as mock_cache:

            mock_settings.spool_enabled = False
            mock_settings.telemetry_max_batch_meta_bytes = 1024 * 1024
            mock_settings.telemetry_max_reading_meta_bytes = 1024 * 1024
            mock_cache.get_active_profile = AsyncMock(return_value=None)

            accepted = await service.ingest(payload, token="sensor-token")

        # All 3 are unique by (sensor_id, timestamp, signal).
        assert accepted == 3
