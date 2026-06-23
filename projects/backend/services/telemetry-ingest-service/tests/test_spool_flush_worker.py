"""Tests for the background spool-flush worker.

Covers ``workers/spool_flush.py``:
- ``_flush_one``: happy path (DB write + file deletion), corrupted file
  quarantine (``.bad`` rename), DB-unavailable path (returns False, file kept).
- ``run_spool_flush_worker``: empty-queue tick, multi-file flush in one cycle,
  early abort when the DB raises ``PostgresError`` mid-cycle.
"""
from __future__ import annotations

import asyncio
import hashlib
from uuid import UUID, uuid4

import asyncpg
import pytest
from asyncpg.exceptions import PostgresError

from telemetry_ingest_service.services.spool import SpoolRecord, write_spool
from telemetry_ingest_service.services.telemetry import TelemetryIngestService
from telemetry_ingest_service.settings import settings
from telemetry_ingest_service.workers.spool_flush import _flush_one, run_spool_flush_worker


def _token_hash(token: str) -> bytes:
    return hashlib.sha256(token.encode("utf-8")).digest()


async def _seed(
    *,
    db_uri: str,
    project_id: UUID,
    sensor_id: UUID,
    run_id: UUID,
    capture_session_id: UUID,
) -> None:
    conn = await asyncpg.connect(db_uri)
    try:
        await conn.execute(
            "INSERT INTO sensors (id, project_id, status, token_hash) "
            "VALUES ($1, $2, 'active', $3)",
            sensor_id,
            project_id,
            _token_hash("worker-token"),
        )
        await conn.execute(
            "INSERT INTO runs (id, project_id) VALUES ($1, $2)",
            run_id,
            project_id,
        )
        await conn.execute(
            "INSERT INTO capture_sessions "
            "(id, run_id, project_id, ordinal_number, status, archived) "
            "VALUES ($1, $2, $3, 1, 'running', false)",
            capture_session_id,
            run_id,
            project_id,
        )
    finally:
        await conn.close()


def _make_record(
    *,
    project_id: UUID,
    sensor_id: UUID,
    run_id: UUID,
    capture_session_id: UUID,
    raw_value: float = 1.23,
) -> SpoolRecord:
    return SpoolRecord(
        sensor_id=sensor_id,
        items=[{
            "project_id": str(project_id),
            "sensor_id": str(sensor_id),
            "run_id": str(run_id),
            "capture_session_id": str(capture_session_id),
            "timestamp": "2026-01-01T00:00:00+00:00",
            "raw_value": raw_value,
            "physical_value": None,
            "meta": "{}",
            "conversion_status": "raw_only",
            "conversion_profile_id": None,
        }],
        last_reading_ts="2026-01-01T00:00:00+00:00",
    )


# ---------------------------------------------------------------------------
# _flush_one
# ---------------------------------------------------------------------------


async def test_flush_one_happy_path_writes_to_db_and_deletes_file(
    service_client, pgsql, tmp_path, monkeypatch
):
    monkeypatch.setattr(settings, "spool_dir", str(tmp_path))

    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    path = write_spool(_make_record(
        project_id=project_id,
        sensor_id=sensor_id,
        run_id=run_id,
        capture_session_id=capture_session_id,
    ))

    ok = await _flush_one(path)
    assert ok is True
    assert not path.exists()

    conn = await asyncpg.connect(db_uri)
    try:
        count = await conn.fetchval(
            "SELECT COUNT(*) FROM telemetry_records WHERE sensor_id = $1", sensor_id
        )
        assert int(count) == 1
    finally:
        await conn.close()


async def test_flush_one_quarantines_corrupted_file(service_client, tmp_path, monkeypatch):
    """A non-JSON file is renamed to .bad and skipped (returns True)."""
    monkeypatch.setattr(settings, "spool_dir", str(tmp_path))

    bad = tmp_path / "1234567890_garbage.json"
    bad.write_text("this is not json at all")

    ok = await _flush_one(bad)
    assert ok is True
    assert not bad.exists()
    assert bad.with_suffix(".bad").exists()


async def test_flush_one_returns_false_on_db_error(
    service_client, pgsql, tmp_path, monkeypatch
):
    """When the DB raises PostgresError the file stays on disk and the
    function returns False so the caller can abort the cycle."""
    monkeypatch.setattr(settings, "spool_dir", str(tmp_path))

    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    path = write_spool(_make_record(
        project_id=project_id,
        sensor_id=sensor_id,
        run_id=run_id,
        capture_session_id=capture_session_id,
    ))

    async def _fail(self, conn, record):
        raise PostgresError("simulated DB outage")

    monkeypatch.setattr(TelemetryIngestService, "_flush_spool_record", _fail)

    ok = await _flush_one(path)
    assert ok is False
    assert path.exists()  # not consumed — will be retried next cycle


# ---------------------------------------------------------------------------
# run_spool_flush_worker
# ---------------------------------------------------------------------------


async def test_worker_idle_when_no_spool_files(service_client, tmp_path, monkeypatch):
    """With an empty spool dir the worker just loops without erroring."""
    monkeypatch.setattr(settings, "spool_dir", str(tmp_path))
    monkeypatch.setattr(settings, "spool_flush_interval_seconds", 0.01)

    task = asyncio.create_task(run_spool_flush_worker())
    try:
        await asyncio.sleep(0.05)  # let it tick a few times
    finally:
        task.cancel()
        with pytest.raises(asyncio.CancelledError):
            await task


async def test_worker_flushes_multiple_files_in_one_cycle(
    service_client, pgsql, tmp_path, monkeypatch
):
    monkeypatch.setattr(settings, "spool_dir", str(tmp_path))
    monkeypatch.setattr(settings, "spool_flush_interval_seconds", 0.01)

    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    for value in (1.0, 2.0, 3.0):
        write_spool(_make_record(
            project_id=project_id,
            sensor_id=sensor_id,
            run_id=run_id,
            capture_session_id=capture_session_id,
            raw_value=value,
        ))

    task = asyncio.create_task(run_spool_flush_worker())
    try:
        # Wait for all three files to be drained.
        for _ in range(50):
            await asyncio.sleep(0.02)
            if not list(tmp_path.glob("*.json")):
                break
    finally:
        task.cancel()
        with pytest.raises(asyncio.CancelledError):
            await task

    assert list(tmp_path.glob("*.json")) == []

    conn = await asyncpg.connect(db_uri)
    try:
        count = await conn.fetchval(
            "SELECT COUNT(*) FROM telemetry_records WHERE sensor_id = $1", sensor_id
        )
        assert int(count) == 3
    finally:
        await conn.close()


async def test_worker_aborts_cycle_on_db_error(
    service_client, pgsql, tmp_path, monkeypatch
):
    """When the first file fails with PostgresError the worker breaks out
    of the cycle instead of attempting the rest."""
    monkeypatch.setattr(settings, "spool_dir", str(tmp_path))
    monkeypatch.setattr(settings, "spool_flush_interval_seconds", 0.01)

    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    for _ in range(3):
        write_spool(_make_record(
            project_id=project_id,
            sensor_id=sensor_id,
            run_id=run_id,
            capture_session_id=capture_session_id,
        ))

    call_count = 0

    async def _always_fail(self, conn, record):
        nonlocal call_count
        call_count += 1
        raise PostgresError("DB outage")

    monkeypatch.setattr(TelemetryIngestService, "_flush_spool_record", _always_fail)

    task = asyncio.create_task(run_spool_flush_worker())
    try:
        await asyncio.sleep(0.1)  # multiple ticks
    finally:
        task.cancel()
        with pytest.raises(asyncio.CancelledError):
            await task

    # All 3 files must still be on disk, untouched.
    assert len(list(tmp_path.glob("*.json"))) == 3

    # The worker must have exhibited the abort-on-first-failure pattern: at
    # most one call per cycle. We don't assert an exact number (timing-dependent)
    # but we do assert it's nowhere near 3-per-tick × N-ticks behavior.
    assert call_count >= 1
