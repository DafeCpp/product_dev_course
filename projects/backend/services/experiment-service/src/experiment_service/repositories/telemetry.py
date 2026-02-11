"""Telemetry persistence repository."""
from __future__ import annotations

import json
from typing import Sequence
from uuid import UUID

from asyncpg import Pool  # type: ignore[import-untyped]

from experiment_service.domain.dto import TelemetryRecordCreateDTO
from experiment_service.repositories.base import BaseRepository


class TelemetryRepository(BaseRepository):
    """Persists telemetry readings in bulk."""

    def __init__(self, pool: Pool):
        super().__init__(pool)

    async def bulk_insert(self, records: Sequence[TelemetryRecordCreateDTO]) -> None:
        if not records:
            return
        payloads = [
            (
                record.project_id,
                record.sensor_id,
                record.run_id,
                record.capture_session_id,
                record.timestamp,
                record.raw_value,
                record.physical_value,
                json.dumps(record.meta),
                record.conversion_status.value,
                record.conversion_profile_id,
            )
            for record in records
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
        async with self._pool.acquire() as conn:
            await conn.executemany(query, payloads)

    async def attach_late_records(self, capture_session_id: UUID) -> int:
        """Attach late telemetry records to a capture session.

        Finds records where ``meta.__system.capture_session_id`` matches
        but ``capture_session_id`` column is NULL (late data), then sets the
        column and clears the ``late`` / ``capture_session_attached`` markers
        in ``meta.__system``.

        Returns the number of updated rows.
        """
        query = """
            UPDATE telemetry_records
            SET capture_session_id = $1,
                meta = jsonb_set(
                    jsonb_set(
                        meta,
                        '{__system,late}',
                        'false'::jsonb,
                        false
                    ),
                    '{__system,capture_session_attached}',
                    'true'::jsonb,
                    true
                )
            WHERE capture_session_id IS NULL
              AND (meta->'__system'->>'capture_session_id') = $2::text
        """
        async with self._pool.acquire() as conn:
            result = await conn.execute(query, capture_session_id, str(capture_session_id))
            # asyncpg returns "UPDATE N"
            return int(result.split()[-1])

