"""Read-side persistence for telemetry endpoints."""
from __future__ import annotations

from datetime import datetime
from typing import Any, Sequence
from uuid import UUID

from asyncpg import Pool  # type: ignore[import-untyped]

from backend_common.repositories.base import BaseRepository


class TelemetryReadRepository(BaseRepository):
    """Queries telemetry data and resolves its project ownership."""

    def __init__(self, pool: Pool) -> None:
        super().__init__(pool)

    async def get_sensor_project(self, sensor_id: UUID, token_hash: bytes | None = None) -> UUID | None:
        if token_hash is None:
            row = await self._fetchrow("SELECT project_id FROM sensors WHERE id = $1", sensor_id)
        else:
            row = await self._fetchrow(
                "SELECT project_id FROM sensors WHERE id = $1 AND token_hash = $2", sensor_id, token_hash
            )
        return UUID(str(row["project_id"])) if row is not None else None

    async def get_capture_session_project(self, capture_session_id: UUID) -> UUID | None:
        row = await self._fetchrow(
            "SELECT project_id FROM capture_sessions WHERE id = $1", capture_session_id
        )
        return UUID(str(row["project_id"])) if row is not None else None

    async def list_stream_records(
        self, project_id: UUID, sensor_id: UUID, since_ts: datetime, since_id: int
    ) -> list[dict[str, Any]]:
        rows = await self._fetch(
            """
            SELECT id, timestamp, raw_value, physical_value, meta, run_id, capture_session_id
            FROM telemetry_records
            WHERE project_id = $1 AND sensor_id = $2 AND (timestamp, id) > ($3, $4)
            ORDER BY timestamp ASC, id ASC
            LIMIT 100
            """,
            project_id, sensor_id, since_ts, since_id,
        )
        return [dict(row) for row in rows]

    async def list_records(
        self, capture_session_id: UUID, sensor_ids: Sequence[UUID], since_id: int,
        limit: int, include_late: bool, order: str,
    ) -> list[dict[str, Any]]:
        conditions = []
        params: list[object] = [capture_session_id]
        if include_late:
            conditions.append("(capture_session_id = $1 OR (meta->'__system'->>'capture_session_id') = $1::text)")
        else:
            conditions.extend(("capture_session_id = $1", "COALESCE((meta->'__system'->>'late')::boolean, false) = false"))
        if order == "asc" or since_id > 0:
            params.append(since_id)
            conditions.append(f"id {'>' if order == 'asc' else '<'} ${len(params)}")
        if sensor_ids:
            params.append(list(sensor_ids))
            conditions.append(f"sensor_id = ANY(${len(params)}::uuid[])")
        params.append(limit)
        rows = await self._fetch(
            f"""
            SELECT id, project_id, sensor_id, timestamp, raw_value, physical_value, run_id, capture_session_id, meta
            FROM telemetry_records
            WHERE {' AND '.join(conditions)}
            ORDER BY id {'ASC' if order == 'asc' else 'DESC'}
            LIMIT ${len(params)}
            """,
            *params,
        )
        return [dict(row) for row in rows]

    async def list_aggregated_records(
        self, capture_session_id: UUID, sensor_ids: Sequence[UUID], signal: str | None,
        time_from: datetime | None, time_to: datetime | None, limit: int, order: str,
    ) -> list[dict[str, Any]]:
        conditions = ["capture_session_id = $1"]
        params: list[object] = [capture_session_id]
        for condition, value in (("sensor_id = ANY({}::uuid[])", list(sensor_ids) if sensor_ids else None),
                                 ("signal = {}", signal), ("bucket >= {}", time_from), ("bucket <= {}", time_to)):
            if value is not None:
                params.append(value)
                conditions.append(condition.format(f"${len(params)}"))
        params.append(limit)
        rows = await self._fetch(
            f"""
            SELECT bucket, sensor_id, signal, capture_session_id, sample_count, avg_raw, min_raw, max_raw,
                   avg_physical, min_physical, max_physical
            FROM telemetry_1m
            WHERE {' AND '.join(conditions)}
            ORDER BY bucket {'ASC' if order == 'asc' else 'DESC'}
            LIMIT ${len(params)}
            """,
            *params,
        )
        return [dict(row) for row in rows]
