"""Telemetry ingest business logic."""
from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from datetime import datetime
from typing import Sequence
from uuid import UUID

from backend_common.db.pool import get_pool_service as get_pool

from telemetry_ingest_service.core.exceptions import (
    NotFoundError,
    ScopeMismatchError,
    UnauthorizedError,
)
from telemetry_ingest_service.domain.dto import TelemetryIngestDTO, TelemetryReadingDTO


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

            run_id = await self._ensure_run_scope(conn, project_id, payload.run_id)
            capture_run_id = await self._ensure_capture_scope(
                conn, project_id, payload.capture_session_id
            )

            if run_id and capture_run_id and run_id != capture_run_id:
                raise ScopeMismatchError("Capture session does not belong to specified run")

            if run_id is None:
                run_id = capture_run_id

            await self._bulk_insert(conn, project_id, payload, run_id=run_id)
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

    async def _bulk_insert(
        self,
        conn,
        project_id: UUID,
        payload: TelemetryIngestDTO,
        *,
        run_id: UUID | None,
    ) -> None:
        batch_meta = payload.meta or {}
        items = [
            (
                project_id,
                payload.sensor_id,
                run_id,
                payload.capture_session_id,
                reading.timestamp,
                reading.raw_value,
                reading.physical_value,
                json.dumps({**batch_meta, **reading.meta}),
                "client_provided" if reading.physical_value is not None else "raw_only",
                None,  # conversion_profile_id (MVP: not computed here)
            )
            for reading in payload.readings
        ]
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

