"""Telemetry ingest business logic."""
from __future__ import annotations

import hashlib
import json
import structlog
from dataclasses import dataclass
from datetime import datetime
from typing import Any, Sequence, cast
from uuid import UUID

from backend_common.conversion import apply_conversion
from backend_common.db.pool import get_pool_service as get_pool

from telemetry_ingest_service.core.exceptions import (
    NotFoundError,
    ScopeMismatchError,
    UnauthorizedError,
)
from telemetry_ingest_service.domain.dto import TelemetryIngestDTO, TelemetryReadingDTO
from telemetry_ingest_service.services.profile_cache import profile_cache
from telemetry_ingest_service.settings import settings

logger = structlog.get_logger(__name__)


def hash_sensor_token(token: str) -> bytes:
    return hashlib.sha256(token.encode("utf-8")).digest()


@dataclass(frozen=True, slots=True)
class _SensorAuth:
    project_id: UUID


class TelemetryIngestService:
    """Validates telemetry payloads and persists telemetry_records."""

    async def ingest(self, payload: TelemetryIngestDTO, *, token: str) -> int:
        pool = await get_pool()
        token_hash = hash_sensor_token(token)

        async with pool.acquire() as conn, conn.transaction():
            sensor = await self._authenticate_sensor(conn, payload.sensor_id, token_hash)
            project_id = sensor.project_id

            # Resolve run/capture context.
            # Desired behavior:
            #  - Sensors may send run_id only (recommended); service auto-attaches active capture_session_id.
            #  - If capture_session_id is provided but session is not active, we do NOT attach it to records
            #    (recording should stop after session stop), but we keep a marker in meta.
            run_id = await self._ensure_run_scope(conn, project_id, payload.run_id)

            requested_capture_session_id = payload.capture_session_id
            requested_capture_run_id = await self._ensure_capture_scope(
                conn, project_id, requested_capture_session_id
            )

            if run_id and requested_capture_run_id and run_id != requested_capture_run_id:
                raise ScopeMismatchError("Capture session does not belong to specified run")

            if run_id is None:
                run_id = requested_capture_run_id

            # Sensors are NOT tied to runs/experiments. If client didn't provide run/capture context,
            # we infer it from the *project's* currently active capture session (single recording window).
            inferred_capture: tuple[UUID, UUID, str] | None = None
            if run_id is None and requested_capture_session_id is None:
                inferred_capture = await self._find_active_capture_session_in_project(conn, project_id)
                if inferred_capture is not None:
                    inferred_run_id, inferred_capture_id, inferred_status = inferred_capture
                    run_id = inferred_run_id
                    requested_capture_session_id = inferred_capture_id
                    requested_capture_run_id = inferred_run_id

            stored_capture_session_id: UUID | None = None
            capture_status: str | None = None
            system_meta: dict[str, object] = {}

            if requested_capture_session_id is not None:
                capture_status = await self._get_capture_status(conn, project_id, requested_capture_session_id)
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

            # If we inferred a capture session id, always mark it (debug/traceability).
            if inferred_capture is not None and "__system" not in system_meta:
                system_meta = {"__system": {"capture_session_inferred_from_project": True}}

            await self._bulk_insert(
                conn,
                project_id,
                payload,
                run_id=run_id,
                capture_session_id=stored_capture_session_id,
                capture_status=capture_status,
                system_meta=system_meta,
            )
            await self._update_sensor_heartbeat(conn, payload.sensor_id, payload.readings)

        return len(payload.readings)

    async def _authenticate_sensor(self, conn, sensor_id: UUID, token_hash: bytes) -> _SensorAuth:
        row = await conn.fetchrow(
            "SELECT project_id FROM sensors WHERE id = $1 AND token_hash = $2",
            sensor_id,
            token_hash,
        )
        if row is None:
            # Do not leak whether sensor exists
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
            "SELECT run_id, status, archived FROM capture_sessions WHERE project_id = $1 AND id = $2",
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
        """
        Find the project's active capture session (single recording window).
        Returns (run_id, capture_session_id, status).
        """
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

    @staticmethod
    def _json_size_bytes(value: object) -> int:
        # Approximate payload size in bytes after json serialization.
        # Keep stable formatting to be predictable.
        return len(json.dumps(value, separators=(",", ":"), ensure_ascii=False).encode("utf-8"))

    def _validate_meta_sizes(self, payload: TelemetryIngestDTO) -> None:
        if self._json_size_bytes(payload.meta or {}) > settings.telemetry_max_batch_meta_bytes:
            raise ScopeMismatchError("Batch meta is too large")
        for reading in payload.readings:
            if self._json_size_bytes(reading.meta or {}) > settings.telemetry_max_reading_meta_bytes:
                raise ScopeMismatchError("Reading meta is too large")

    async def _bulk_insert(
        self,
        conn,
        project_id: UUID,
        payload: TelemetryIngestDTO,
        *,
        run_id: UUID | None,
        capture_session_id: UUID | None,
        capture_status: str | None,
        system_meta: dict[str, Any] | None = None,
    ) -> None:
        self._validate_meta_sizes(payload)
        batch_meta = payload.meta or {}
        if system_meta:
            # merge, with system_meta taking precedence for __system
            if "__system" in system_meta and "__system" in batch_meta and isinstance(batch_meta.get("__system"), dict):
                base_sys = cast(dict[str, Any], batch_meta.get("__system", {}))
                next_sys = cast(dict[str, Any], system_meta.get("__system", {}))
                merged_sys = {**base_sys, **next_sys}
                batch_meta = {**batch_meta, **system_meta, "__system": merged_sys}
            else:
                batch_meta = {**batch_meta, **system_meta}

        is_late = False
        if capture_status is not None:
            status_lower = capture_status.lower()
            # Consider data late if session is already finalized.
            is_late = status_lower in ("succeeded", "failed")

        def _with_late_marker(meta: dict) -> dict:
            if not is_late:
                return meta
            sys_meta = meta.get("__system")
            if isinstance(sys_meta, dict):
                return {**meta, "__system": {**sys_meta, "late": True}}
            return {**meta, "__system": {"late": True}}

        # Load active conversion profile for this sensor (cached).
        active_profile = await profile_cache.get_active_profile(conn, payload.sensor_id)

        items = []
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
        query = """
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
        """
        await conn.executemany(query, items)

    async def _update_sensor_heartbeat(
        self, conn, sensor_id: UUID, readings: Sequence[TelemetryReadingDTO]
    ) -> None:
        last_timestamp: datetime = max(r.timestamp for r in readings)
        await conn.execute(
            """
            UPDATE sensors
            SET status = 'active',
                last_heartbeat = $2,
                updated_at = now()
            WHERE id = $1
            """,
            sensor_id,
            last_timestamp,
        )

