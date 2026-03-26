"""Tests for the disk spool subsystem.

Unit tests (no DB):
  - write/read roundtrip
  - FIFO ordering of list_spool_files
  - spool_max_files limit

Integration tests (with DB):
  - ingest falls back to spool when _do_insert raises PostgresError
  - flush worker (_flush_spool_record) replays spooled data into the DB
"""
from __future__ import annotations

import hashlib
import json
import time
from datetime import datetime, timezone
from uuid import UUID, uuid4

import asyncpg
import pytest
from asyncpg.exceptions import PostgresError

from telemetry_ingest_service.services.spool import (
    SpoolRecord,
    delete_spool,
    list_spool_files,
    read_spool,
    write_spool,
)
from telemetry_ingest_service.services.telemetry import TelemetryIngestService
from telemetry_ingest_service.settings import settings


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _token_hash(token: str) -> bytes:
    return hashlib.sha256(token.encode("utf-8")).digest()


async def _seed(
    *,
    db_uri: str,
    project_id: UUID,
    sensor_id: UUID,
    token: str,
    run_id: UUID,
    capture_session_id: UUID,
) -> None:
    conn = await asyncpg.connect(db_uri)
    try:
        await conn.execute(
            "INSERT INTO sensors (id, project_id, status, token_hash) VALUES ($1, $2, 'active', $3)",
            sensor_id,
            project_id,
            _token_hash(token),
        )
        await conn.execute(
            "INSERT INTO runs (id, project_id) VALUES ($1, $2)",
            run_id,
            project_id,
        )
        await conn.execute(
            """
            INSERT INTO capture_sessions (id, run_id, project_id, ordinal_number, status, archived)
            VALUES ($1, $2, $3, 1, 'running', false)
            """,
            capture_session_id,
            run_id,
            project_id,
        )
    finally:
        await conn.close()


def _make_item(
    *,
    project_id: UUID,
    sensor_id: UUID,
    run_id: UUID,
    capture_session_id: UUID,
) -> dict:
    return {
        "project_id": str(project_id),
        "sensor_id": str(sensor_id),
        "run_id": str(run_id),
        "capture_session_id": str(capture_session_id),
        "timestamp": "2026-01-01T00:00:00+00:00",
        "raw_value": 1.23,
        "physical_value": None,
        "meta": "{}",
        "conversion_status": "raw_only",
        "conversion_profile_id": None,
    }


# ---------------------------------------------------------------------------
# Unit tests — no DB required
# ---------------------------------------------------------------------------


def test_spool_write_read_roundtrip(tmp_path, monkeypatch):
    monkeypatch.setattr(settings, "spool_dir", str(tmp_path))
    sensor_id = uuid4()
    record = SpoolRecord(
        sensor_id=sensor_id,
        items=[
            {
                "project_id": str(uuid4()),
                "sensor_id": str(sensor_id),
                "run_id": None,
                "capture_session_id": None,
                "timestamp": "2026-01-01T00:00:00+00:00",
                "raw_value": 7.0,
                "physical_value": 14.0,
                "meta": '{"signal":"rpm"}',
                "conversion_status": "converted",
                "conversion_profile_id": None,
            }
        ],
        last_reading_ts="2026-01-01T00:00:00+00:00",
    )
    path = write_spool(record)
    assert path.exists()

    loaded = read_spool(path)
    assert loaded.sensor_id == record.sensor_id
    assert loaded.items == record.items
    assert loaded.last_reading_ts == record.last_reading_ts


def test_list_spool_files_fifo_order(tmp_path, monkeypatch):
    monkeypatch.setattr(settings, "spool_dir", str(tmp_path))
    sensor_id = uuid4()

    paths = []
    for i in range(3):
        r = SpoolRecord(
            sensor_id=sensor_id,
            items=[],
            last_reading_ts=f"2026-01-01T00:0{i}:00+00:00",
        )
        paths.append(write_spool(r))
        time.sleep(0.001)  # ensure distinct nanosecond timestamps

    listed = list_spool_files()
    assert [p.name for p in listed] == sorted(p.name for p in paths)


def test_spool_max_files_raises(tmp_path, monkeypatch):
    monkeypatch.setattr(settings, "spool_dir", str(tmp_path))
    monkeypatch.setattr(settings, "spool_max_files", 2)
    sensor_id = uuid4()

    for _ in range(2):
        write_spool(SpoolRecord(sensor_id=sensor_id, items=[], last_reading_ts="2026-01-01T00:00:00+00:00"))

    with pytest.raises(RuntimeError, match="Spool is full"):
        write_spool(SpoolRecord(sensor_id=sensor_id, items=[], last_reading_ts="2026-01-01T00:00:00+00:00"))


def test_delete_spool_is_idempotent(tmp_path, monkeypatch):
    monkeypatch.setattr(settings, "spool_dir", str(tmp_path))
    r = SpoolRecord(sensor_id=uuid4(), items=[], last_reading_ts="2026-01-01T00:00:00+00:00")
    path = write_spool(r)
    delete_spool(path)
    delete_spool(path)  # must not raise


# ---------------------------------------------------------------------------
# Integration tests — DB required
# ---------------------------------------------------------------------------


async def test_ingest_spools_on_db_write_failure(service_client, pgsql, tmp_path, monkeypatch):
    """When _do_insert raises PostgresError, the batch is spooled and
    the endpoint returns 202 (optimistic success)."""
    monkeypatch.setattr(settings, "spool_dir", str(tmp_path))
    monkeypatch.setattr(settings, "spool_enabled", True)

    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()
    token = "spool-test-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    # Make the INSERT step fail.
    async def _failing_do_insert(self, conn, items):
        raise PostgresError("simulated write failure")

    monkeypatch.setattr(TelemetryIngestService, "_do_insert", _failing_do_insert)

    resp = await service_client.post(
        "/api/v1/telemetry",
        json={
            "sensor_id": str(sensor_id),
            "run_id": str(run_id),
            "capture_session_id": str(capture_session_id),
            "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.23}],
        },
        headers={"Authorization": f"Bearer {token}"},
    )
    assert resp.status == 202

    # Spool directory must contain exactly one file.
    spool_files = list(tmp_path.glob("*.json"))
    assert len(spool_files) == 1

    # DB must have no telemetry records for this sensor.
    conn = await asyncpg.connect(db_uri)
    try:
        count = await conn.fetchval(
            "SELECT COUNT(*) FROM telemetry_records WHERE sensor_id = $1", sensor_id
        )
        assert int(count) == 0
    finally:
        await conn.close()


async def test_spool_flush_replays_data_into_db(service_client, pgsql, tmp_path, monkeypatch):
    """_flush_spool_record inserts a spooled batch and updates the heartbeat."""
    monkeypatch.setattr(settings, "spool_dir", str(tmp_path))

    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()
    token = "flush-test-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    # Build a SpoolRecord manually (as if a prior ingest had spooled it).
    record = SpoolRecord(
        sensor_id=sensor_id,
        items=[_make_item(
            project_id=project_id,
            sensor_id=sensor_id,
            run_id=run_id,
            capture_session_id=capture_session_id,
        )],
        last_reading_ts="2026-01-01T00:00:00+00:00",
    )

    # Replay via the service (simulates what the flush worker does).
    from backend_common.db.pool import get_pool_service as get_pool
    pool = await get_pool()
    service = TelemetryIngestService()
    async with pool.acquire() as conn:
        async with conn.transaction():
            await service._flush_spool_record(conn, record)

    # Verify the record is now in the DB.
    conn = await asyncpg.connect(db_uri)
    try:
        count = await conn.fetchval(
            "SELECT COUNT(*) FROM telemetry_records WHERE sensor_id = $1", sensor_id
        )
        assert int(count) == 1

        row = await conn.fetchrow(
            "SELECT raw_value, run_id, capture_session_id "
            "FROM telemetry_records WHERE sensor_id = $1",
            sensor_id,
        )
        assert row is not None
        assert abs(float(row["raw_value"]) - 1.23) < 1e-9
        assert str(row["run_id"]) == str(run_id)
        assert str(row["capture_session_id"]) == str(capture_session_id)

        # Heartbeat must have been updated.
        hb = await conn.fetchval("SELECT last_heartbeat FROM sensors WHERE id = $1", sensor_id)
        assert hb is not None
    finally:
        await conn.close()


async def test_ingest_spool_disabled_raises_on_write_failure(
    service_client, pgsql, tmp_path, monkeypatch
):
    """When spool_enabled=False, a DB write failure propagates as 500."""
    monkeypatch.setattr(settings, "spool_dir", str(tmp_path))
    monkeypatch.setattr(settings, "spool_enabled", False)

    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()
    token = "no-spool-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    async def _failing_do_insert(self, conn, items):
        raise PostgresError("simulated write failure")

    monkeypatch.setattr(TelemetryIngestService, "_do_insert", _failing_do_insert)

    resp = await service_client.post(
        "/api/v1/telemetry",
        json={
            "sensor_id": str(sensor_id),
            "run_id": str(run_id),
            "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0}],
        },
        headers={"Authorization": f"Bearer {token}"},
    )
    assert resp.status == 500

    # No spool file must have been created.
    assert list(tmp_path.glob("*.json")) == []
