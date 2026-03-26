"""Telemetry ingest business logic."""
from __future__ import annotations

import asyncio
import hashlib
import json
import structlog
from dataclasses import dataclass
from datetime import datetime
from typing import Any, Sequence, cast
from uuid import UUID

from asyncpg.exceptions import PostgresError  # type: ignore[import-untyped]

from backend_common.conversion import apply_conversion
from backend_common.db.pool import get_pool_service as get_pool

from telemetry_ingest_service.core.exceptions import (
    NotFoundError,
    ScopeMismatchError,
    UnauthorizedError,
)
from telemetry_ingest_service.domain.dto import TelemetryIngestDTO, TelemetryReadingDTO
from telemetry_ingest_service.services.profile_cache import profile_cache
from telemetry_ingest_service.services.spool import SpoolRecord, write_spool
from telemetry_ingest_service.settings import settings

logger = structlog.get_logger(__name__)


def hash_sensor_token(token: str) -> bytes:
    return hashlib.sha256(token.encode("utf-8")).digest()


@dataclass(frozen=True, slots=True)
class _SensorAuth:
    project_id: UUID


# ---------------------------------------------------------------------------
# INSERT query — shared with the flush worker
# ---------------------------------------------------------------------------

_INSERT_QUERY = """
    INSERT INTO telemetry_records (
        project_id,
        sensor_id,
        run_id,
        capture_session_id,
        timestamp,
        raw_value,
        physical_value,
        meta,
        conversion_status,
        conversion_profile_id
    )
    VALUES ($1, $2, $3, $4, $5, $6, $7, $8::jsonb, $9, $10)
    ON CONFLICT (sensor_id, timestamp, signal) DO NOTHING
"""


# ---------------------------------------------------------------------------
# Spool serialization helpers
# ---------------------------------------------------------------------------

def _items_to_dicts(items: list[tuple]) -> list[dict[str, Any]]:
    """Convert INSERT tuples to JSON-serializable dicts for spool storage."""
    result: list[dict[str, Any]] = []
    for (proj_id, sens_id, run_id, cap_id, ts, raw, phys, meta, conv_st, conv_prof) in items:
        result.append({
            "project_id": str(proj_id),
            "sensor_id": str(sens_id),
            "run_id": str(run_id) if run_id else None,
            "capture_session_id": str(cap_id) if cap_id else None,
            "timestamp": ts.isoformat() if isinstance(ts, datetime) else str(ts),
            "raw_value": raw,
            "physical_value": phys,
            "meta": meta,  # already a JSON string
            "conversion_status": conv_st,
            "conversion_profile_id": str(conv_prof) if conv_prof else None,
        })
    return result


def items_from_dicts(dicts: list[dict[str, Any]]) -> list[tuple]:
    """Restore INSERT tuples from spool item dicts."""
    result: list[tuple] = []
    for item in dicts:
        ts_raw = item["timestamp"]
        ts = datetime.fromisoformat(ts_raw) if isinstance(ts_raw, str) else ts_raw
        result.append((
            UUID(item["project_id"]),
            UUID(item["sensor_id"]),
            UUID(item["run_id"]) if item.get("run_id") else None,
            UUID(item["capture_session_id"]) if item.get("capture_session_id") else None,
            ts,
            item["raw_value"],
            item["physical_value"],
            item["meta"],  # JSON string — asyncpg handles the ::jsonb cast
            item["conversion_status"],
            UUID(item["conversion_profile_id"]) if item.get("conversion_profile_id") else None,
        ))
    return result


# ---------------------------------------------------------------------------
# Service
# ---------------------------------------------------------------------------

class TelemetryIngestService:
    """Validates telemetry payloads and persists telemetry_records.

    ``ingest()`` runs in two phases:

    Phase 1 (reads):
        Authenticate the sensor, resolve run/capture context, compute
        insertion rows (including conversion profile application from cache).
        All DB access is read-only; no explicit transaction is needed.

    Phase 2 (write):
        Execute the bulk INSERT + sensor heartbeat UPDATE inside a single
        transaction.  If this phase raises ``PostgresError`` and
        ``settings.spool_enabled`` is True, the pre-computed rows are
        serialised to a spool file on disk for later replay by the flush
        worker.  The caller receives an optimistic 202 response.
    """

    # ------------------------------------------------------------------
    # Public interface
    # ------------------------------------------------------------------

    async def ingest(self, payload: TelemetryIngestDTO, *, token: str) -> int:
        pool = await get_pool()
        token_hash = hash_sensor_token(token)

        # --- Phase 1: reads (auth + scope resolution + row preparation) ---
        async with pool.acquire() as conn:
            sensor = await self._authenticate_sensor(conn, payload.sensor_id, token_hash)
            project_id = sensor.project_id

            run_id = await self._ensure_run_scope(conn, project_id, payload.run_id)

            requested_capture_session_id = payload.capture_session_id
            requested_capture_run_id = await self._ensure_capture_scope(
                conn, project_id, requested_capture_session_id
            )

            if run_id and requested_capture_run_id and run_id != requested_capture_run_id:
                raise ScopeMismatchError("Capture session does not belong to specified run")

            if run_id is None:
                run_id = requested_capture_run_id

            inferred_capture: tuple[UUID, UUID, str] | None = None
            if run_id is None and requested_capture_session_id is None:
                inferred_capture = await self._find_active_capture_session_in_project(
                    conn, project_id
                )
                if inferred_capture is not None:
                    inferred_run_id, inferred_capture_id, _ = inferred_capture
                    run_id = inferred_run_id
                    requested_capture_session_id = inferred_capture_id
                    requested_capture_run_id = inferred_run_id

            stored_capture_session_id: UUID | None = None
            capture_status: str | None = None
            system_meta: dict[str, object] = {}

            if requested_capture_session_id is not None:
                capture_status = await self._get_capture_status(
                    conn, project_id, requested_capture_session_id
                )
                status_lower = (capture_status or "").lower()
                is_active = status_lower in ("running", "backfilling")
                if is_active:
                    stored_capture_session_id = requested_capture_session_id
                else:
                    system_meta = {
                        "__system": {
                            "capture_session_attached": False,
                            "capture_session_id": str(requested_capture_session_id),
                            "capture_session_status": capture_status,
                        }
                    }
            elif run_id is not None:
                active = await self._find_active_capture_session(conn, project_id, run_id)
                if active is not None:
                    stored_capture_session_id, capture_status = active
                    system_meta = {"__system": {"capture_session_auto_attached": True}}

            if inferred_capture is not None and "__system" not in system_meta:
                system_meta = {"__system": {"capture_session_inferred_from_project": True}}

            # Prepare INSERT rows (conversion profile lookup goes via in-memory cache).
            items, last_ts = await self._prepare_items(
                conn,
                project_id,
                payload,
                run_id=run_id,
                capture_session_id=stored_capture_session_id,
                capture_status=capture_status,
                system_meta=system_meta,
            )

        # --- Phase 2: write (INSERT + heartbeat) with spool fallback ---
        try:
            async with pool.acquire() as conn:
                async with conn.transaction():
                    inserted = await self._do_insert(conn, items)
                    await self._update_sensor_heartbeat_ts(conn, payload.sensor_id, last_ts)
        except PostgresError as exc:
            if settings.spool_enabled:
                logger.warning(
                    "ingest_write_failed_spooling",
                    sensor_id=str(payload.sensor_id),
                    error=str(exc),
                )
                record = SpoolRecord(
                    sensor_id=payload.sensor_id,
                    items=_items_to_dicts(items),
                    last_reading_ts=last_ts.isoformat(),
                )
                # File I/O off the event loop to avoid blocking ingest latency.
                await asyncio.to_thread(write_spool, record)
                return len(payload.readings)
            raise

        return inserted

    async def _flush_spool_record(self, conn, record: SpoolRecord) -> int:
        """Replay one spooled batch.  Must be called within an open transaction.

        Returns the number of rows actually inserted (duplicates silently skipped).
        """
        items = items_from_dicts(record.items)
        inserted = await self._do_insert(conn, items)
        last_ts = datetime.fromisoformat(record.last_reading_ts)
        await self._update_sensor_heartbeat_ts(conn, record.sensor_id, last_ts)
        return inserted

    # ------------------------------------------------------------------
    # Auth / scope resolution (read-only DB queries)
    # ------------------------------------------------------------------

    async def _authenticate_sensor(
        self, conn, sensor_id: UUID, token_hash: bytes
    ) -> _SensorAuth:
        row = await conn.fetchrow(
            "SELECT project_id FROM sensors WHERE id = $1 AND token_hash = $2",
            sensor_id,
            token_hash,
        )
        if row is None:
            raise UnauthorizedError("Invalid sensor credentials")
        return _SensorAuth(project_id=UUID(str(row["project_id"])))

    async def _ensure_run_scope(
        self, conn, project_id: UUID, run_id: UUID | None
    ) -> UUID | None:
        if run_id is None:
            return None
        row = await conn.fetchrow(
            "SELECT id, status FROM runs WHERE project_id = $1 AND id = $2",
            project_id,
            run_id,
        )
        if row is None:
            raise NotFoundError("Run not found")
        status = row.get("status")
        if isinstance(status, str) and status.lower() == "archived":
            raise ScopeMismatchError("Run is archived")
        return UUID(str(row["id"]))

    async def _ensure_capture_scope(
        self, conn, project_id: UUID, capture_session_id: UUID | None
    ) -> UUID | None:
        if capture_session_id is None:
            return None
        row = await conn.fetchrow(
            "SELECT run_id, status, archived FROM capture_sessions "
            "WHERE project_id = $1 AND id = $2",
            project_id,
            capture_session_id,
        )
        if row is None:
            raise NotFoundError("Capture session not found")
        status = row.get("status")
        if isinstance(status, str) and status.lower() == "archived":
            raise ScopeMismatchError("Capture session is archived")
        archived = row.get("archived")
        if isinstance(archived, bool) and archived is True:
            raise ScopeMismatchError("Capture session is archived")
        return UUID(str(row["run_id"]))

    async def _get_capture_status(
        self, conn, project_id: UUID, capture_session_id: UUID
    ) -> str | None:
        row = await conn.fetchrow(
            "SELECT status FROM capture_sessions WHERE project_id = $1 AND id = $2",
            project_id,
            capture_session_id,
        )
        if row is None:
            return None
        value = row.get("status")
        return str(value) if value is not None else None

    async def _find_active_capture_session(
        self, conn, project_id: UUID, run_id: UUID
    ) -> tuple[UUID, str] | None:
        row = await conn.fetchrow(
            """
            SELECT id, status
            FROM capture_sessions
            WHERE project_id = $1
              AND run_id = $2
              AND archived = false
              AND status IN ('running', 'backfilling')
            ORDER BY started_at DESC NULLS LAST, created_at DESC
            LIMIT 1
            """,
            project_id,
            run_id,
        )
        if row is None:
            return None
        return UUID(str(row["id"])), str(row["status"])

    async def _find_active_capture_session_in_project(
        self, conn, project_id: UUID
    ) -> tuple[UUID, UUID, str] | None:
        row = await conn.fetchrow(
            """
            SELECT run_id, id AS capture_session_id, status
            FROM capture_sessions
            WHERE project_id = $1
              AND archived = false
              AND status IN ('running', 'backfilling')
            ORDER BY started_at DESC NULLS LAST, created_at DESC
            LIMIT 1
            """,
            project_id,
        )
        if row is None:
            return None
        return (
            UUID(str(row["run_id"])),
            UUID(str(row["capture_session_id"])),
            str(row["status"]),
        )

    # ------------------------------------------------------------------
    # Row preparation and DB write
    # ------------------------------------------------------------------

    @staticmethod
    def _json_size_bytes(value: object) -> int:
        return len(json.dumps(value, separators=(",", ":"), ensure_ascii=False).encode("utf-8"))

    def _validate_meta_sizes(self, payload: TelemetryIngestDTO) -> None:
        if self._json_size_bytes(payload.meta or {}) > settings.telemetry_max_batch_meta_bytes:
            raise ScopeMismatchError("Batch meta is too large")
        for reading in payload.readings:
            if self._json_size_bytes(reading.meta or {}) > settings.telemetry_max_reading_meta_bytes:
                raise ScopeMismatchError("Reading meta is too large")

    async def _prepare_items(
        self,
        conn,
        project_id: UUID,
        payload: TelemetryIngestDTO,
        *,
        run_id: UUID | None,
        capture_session_id: UUID | None,
        capture_status: str | None,
        system_meta: dict[str, Any] | None = None,
    ) -> tuple[list[tuple], datetime]:
        """Build INSERT tuples (with conversion applied).

        Returns ``(items, last_reading_ts)`` where ``last_reading_ts`` is the
        maximum timestamp across all readings, used for the heartbeat UPDATE.
        """
        self._validate_meta_sizes(payload)
        batch_meta = payload.meta or {}
        if system_meta:
            if (
                "__system" in system_meta
                and "__system" in batch_meta
                and isinstance(batch_meta.get("__system"), dict)
            ):
                base_sys = cast(dict[str, Any], batch_meta.get("__system", {}))
                next_sys = cast(dict[str, Any], system_meta.get("__system", {}))
                merged_sys = {**base_sys, **next_sys}
                batch_meta = {**batch_meta, **system_meta, "__system": merged_sys}
            else:
                batch_meta = {**batch_meta, **system_meta}

        is_late = False
        if capture_status is not None:
            is_late = capture_status.lower() in ("succeeded", "failed")

        def _with_late_marker(meta: dict) -> dict:
            if not is_late:
                return meta
            sys_meta = meta.get("__system")
            if isinstance(sys_meta, dict):
                return {**meta, "__system": {**sys_meta, "late": True}}
            return {**meta, "__system": {"late": True}}

        active_profile = await profile_cache.get_active_profile(conn, payload.sensor_id)

        items: list[tuple] = []
        last_ts: datetime | None = None

        for reading in payload.readings:
            physical_value = reading.physical_value
            conversion_status = "client_provided" if physical_value is not None else "raw_only"
            conversion_profile_id = None

            if physical_value is None and active_profile is not None:
                try:
                    result = apply_conversion(
                        active_profile.kind, active_profile.payload, reading.raw_value,
                    )
                    if result is not None:
                        physical_value = result
                        conversion_status = "converted"
                    else:
                        conversion_status = "conversion_failed"
                    conversion_profile_id = active_profile.profile_id
                except Exception:
                    logger.warning(
                        "conversion_apply_error",
                        sensor_id=str(payload.sensor_id),
                        kind=active_profile.kind,
                    )
                    conversion_status = "conversion_failed"
                    conversion_profile_id = active_profile.profile_id
            elif physical_value is not None and active_profile is not None:
                conversion_profile_id = active_profile.profile_id

            if last_ts is None or reading.timestamp > last_ts:
                last_ts = reading.timestamp

            items.append((
                project_id,
                payload.sensor_id,
                run_id,
                capture_session_id,
                reading.timestamp,
                reading.raw_value,
                physical_value,
                json.dumps(_with_late_marker({**batch_meta, **reading.meta})),
                conversion_status,
                conversion_profile_id,
            ))

        assert last_ts is not None  # payload has min_length=1 constraint
        return items, last_ts

    async def _do_insert(self, conn, items: list[tuple]) -> int:
        """Execute bulk INSERT with ON CONFLICT DO NOTHING.

        Returns the number of rows actually inserted (duplicates are skipped).
        asyncpg.executemany does not expose per-statement status tags, so we
        use ``execute`` in a loop and parse the ``INSERT 0 N`` status tag to
        accumulate the real insert count.
        """
        inserted = 0
        for row in items:
            status: str = await conn.execute(_INSERT_QUERY, *row)
            # status has the form "INSERT 0 N" where N is 0 or 1
            try:
                inserted += int(status.split()[-1])
            except (IndexError, ValueError):
                # Defensive fallback — should not happen with a well-formed
                # INSERT statement, but we prefer not to crash ingest.
                inserted += 1
        return inserted

    async def _update_sensor_heartbeat_ts(
        self, conn, sensor_id: UUID, last_ts: datetime
    ) -> None:
        await conn.execute(
            """
            UPDATE sensors
            SET status = 'active',
                last_heartbeat = $2,
                updated_at = now()
            WHERE id = $1
            """,
            sensor_id,
            last_ts,
        )

    async def _update_sensor_heartbeat(
        self, conn, sensor_id: UUID, readings: Sequence[TelemetryReadingDTO]
    ) -> None:
        """Convenience wrapper kept for backwards compatibility."""
        last_ts: datetime = max(r.timestamp for r in readings)
        await self._update_sensor_heartbeat_ts(conn, sensor_id, last_ts)
