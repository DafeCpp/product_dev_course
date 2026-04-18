"""Repository for sensor_error_log table."""
from __future__ import annotations

import json
from dataclasses import dataclass
from datetime import datetime

import asyncpg


@dataclass
class SensorErrorEntry:
    id: int
    sensor_id: str
    occurred_at: datetime
    error_code: str
    error_message: str | None
    endpoint: str
    readings_count: int | None
    meta: dict


class SensorErrorLogRepository:
    def __init__(self, pool: asyncpg.Pool) -> None:
        self._pool = pool

    async def insert(
        self,
        sensor_id: str,
        error_code: str,
        *,
        error_message: str | None = None,
        endpoint: str = "rest",
        readings_count: int | None = None,
        meta: dict | None = None,
    ) -> None:
        async with self._pool.acquire() as conn:
            await conn.execute(
                """
                INSERT INTO sensor_error_log
                    (sensor_id, error_code, error_message, endpoint, readings_count, meta)
                VALUES ($1, $2, $3, $4, $5, $6::jsonb)
                """,
                sensor_id,
                error_code,
                error_message,
                endpoint,
                readings_count,
                json.dumps(meta or {}),
            )

    async def list_for_sensor(
        self,
        sensor_id: str,
        *,
        limit: int = 50,
        offset: int = 0,
    ) -> list[SensorErrorEntry]:
        async with self._pool.acquire() as conn:
            rows = await conn.fetch(
                """
                SELECT id, sensor_id, occurred_at, error_code, error_message,
                       endpoint, readings_count, meta
                FROM sensor_error_log
                WHERE sensor_id = $1
                ORDER BY occurred_at DESC
                LIMIT $2 OFFSET $3
                """,
                sensor_id,
                limit,
                offset,
            )
        return [
            SensorErrorEntry(
                id=r["id"],
                sensor_id=str(r["sensor_id"]),
                occurred_at=r["occurred_at"],
                error_code=r["error_code"],
                error_message=r["error_message"],
                endpoint=r["endpoint"],
                readings_count=r["readings_count"],
                meta=dict(r["meta"]),
            )
            for r in rows
        ]

    async def count_for_sensor(self, sensor_id: str) -> int:
        async with self._pool.acquire() as conn:
            row = await conn.fetchrow(
                "SELECT COUNT(*) FROM sensor_error_log WHERE sensor_id = $1",
                sensor_id,
            )
            return int(row[0]) if row else 0
