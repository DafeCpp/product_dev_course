"""Sensor repository backed by asyncpg."""
from __future__ import annotations

from typing import Any, List, Tuple
from uuid import UUID

from asyncpg import Pool, Record  # type: ignore[import-untyped]

from experiment_service.core.exceptions import NotFoundError
from experiment_service.domain.dto import SensorCreateDTO, SensorUpdateDTO
from experiment_service.domain.models import Sensor
from experiment_service.repositories.base import BaseRepository


class SensorRepository(BaseRepository):
    """CRUD operations for sensors."""

    def __init__(self, pool: Pool):
        super().__init__(pool)

    @staticmethod
    def _to_model(record: Record) -> Sensor:
        payload = dict(record)
        return Sensor.model_validate(payload)

    async def create(
        self,
        data: SensorCreateDTO,
        *,
        token_hash: bytes | None = None,
        token_preview: str | None = None,
    ) -> Sensor:
        record = await self._fetchrow(
            """
            INSERT INTO sensors (
                project_id,
                name,
                type,
                input_unit,
                display_unit,
                status,
                token_hash,
                token_preview,
                calibration_notes
            )
            VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)
            RETURNING *
            """,
            data.project_id,
            data.name,
            data.type,
            data.input_unit,
            data.display_unit,
            data.status.value,
            token_hash,
            token_preview,
            data.calibration_notes,
        )
        assert record is not None
        return self._to_model(record)

    async def get(self, project_id: UUID, sensor_id: UUID) -> Sensor:
        record = await self._fetchrow(
            "SELECT * FROM sensors WHERE project_id = $1 AND id = $2",
            project_id,
            sensor_id,
        )
        if record is None:
            raise NotFoundError("Sensor not found")
        return self._to_model(record)

    async def list_by_project(
        self, project_id: UUID, *, limit: int = 50, offset: int = 0
    ) -> Tuple[List[Sensor], int]:
        records = await self._fetch(
            """
            SELECT *,
                   COUNT(*) OVER() AS total_count
            FROM sensors
            WHERE project_id = $1
            ORDER BY created_at DESC
            LIMIT $2 OFFSET $3
            """,
            project_id,
            limit,
            offset,
        )
        items: List[Sensor] = []
        total: int | None = None
        for rec in records:
            rec_dict = dict(rec)
            total_value = rec_dict.pop("total_count", None)
            if total_value is not None:
                total = int(total_value)
            items.append(Sensor.model_validate(rec_dict))
        if total is None:
            total = await self._count_by_project(project_id)
        return items, total

    async def _count_by_project(self, project_id: UUID) -> int:
        record = await self._fetchrow(
            "SELECT COUNT(*) AS total FROM sensors WHERE project_id = $1",
            project_id,
        )
        return int(record["total"]) if record else 0

    async def update(
        self,
        project_id: UUID,
        sensor_id: UUID,
        updates: SensorUpdateDTO,
    ) -> Sensor:
        payload = updates.model_dump(exclude_none=True)
        if not payload:
            raise ValueError("No fields provided for update")
        assignments = []
        values: list[Any] = []
        idx = 1
        for column, value in payload.items():
            assignments.append(f"{column} = ${idx}")
            values.append(value)
            idx += 1
        assignments.append("updated_at = now()")
        values.extend([project_id, sensor_id])
        record = await self._fetchrow(
            f"""
            UPDATE sensors
            SET {', '.join(assignments)}
            WHERE project_id = ${idx} AND id = ${idx + 1}
            RETURNING *
            """,
            *values,
        )
        if record is None:
            raise NotFoundError("Sensor not found")
        return self._to_model(record)

    async def delete(self, project_id: UUID, sensor_id: UUID) -> None:
        record = await self._fetchrow(
            "DELETE FROM sensors WHERE project_id = $1 AND id = $2 RETURNING id",
            project_id,
            sensor_id,
        )
        if record is None:
            raise NotFoundError("Sensor not found")

    async def rotate_token(
        self,
        project_id: UUID,
        sensor_id: UUID,
        *,
        token_hash: bytes,
        token_preview: str,
    ) -> Sensor:
        record = await self._fetchrow(
            """
            UPDATE sensors
            SET token_hash = $3,
                token_preview = $4,
                updated_at = now()
            WHERE project_id = $1 AND id = $2
            RETURNING *
            """,
            project_id,
            sensor_id,
            token_hash,
            token_preview,
        )
        if record is None:
            raise NotFoundError("Sensor not found")
        return self._to_model(record)

    async def get_by_token(self, sensor_id: UUID, token_hash: bytes) -> Sensor:
        record = await self._fetchrow(
            """
            SELECT * FROM sensors
            WHERE id = $1 AND token_hash = $2
            """,
            sensor_id,
            token_hash,
        )
        if record is None:
            raise NotFoundError("Sensor not found")
        return self._to_model(record)

    async def set_active_profile(
        self,
        project_id: UUID,
        sensor_id: UUID,
        profile_id: UUID | None,
    ) -> Sensor:
        record = await self._fetchrow(
            """
            UPDATE sensors
            SET active_profile_id = $3,
                updated_at = now()
            WHERE project_id = $1 AND id = $2
            RETURNING *
            """,
            project_id,
            sensor_id,
            profile_id,
        )
        if record is None:
            raise NotFoundError("Sensor not found")
        return self._to_model(record)
