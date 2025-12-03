"""Conversion profile repository."""
from __future__ import annotations

import json
from typing import Any, List, Tuple
from uuid import UUID

from asyncpg import Pool, Record  # type: ignore[import-untyped]

from experiment_service.core.exceptions import InvalidStatusTransitionError, NotFoundError
from experiment_service.domain.dto import ConversionProfileCreateDTO
from experiment_service.domain.enums import ConversionProfileStatus
from experiment_service.domain.models import ConversionProfile
from experiment_service.repositories.base import BaseRepository


class ConversionProfileRepository(BaseRepository):
    """CRUD helpers for conversion profiles."""

    JSONB_COLUMNS = {"payload"}

    def __init__(self, pool: Pool):
        super().__init__(pool)

    @staticmethod
    def _to_model(record: Record) -> ConversionProfile:
        payload = dict(record)
        for column in ConversionProfileRepository.JSONB_COLUMNS:
            value = payload.get(column)
            if isinstance(value, str):
                payload[column] = json.loads(value)
        return ConversionProfile.model_validate(payload)

    async def create(self, data: ConversionProfileCreateDTO) -> ConversionProfile:
        record = await self._fetchrow(
            """
            INSERT INTO conversion_profiles (
                sensor_id,
                project_id,
                version,
                kind,
                payload,
                status,
                valid_from,
                valid_to,
                created_by
            )
            VALUES ($1, $2, $3, $4, $5::jsonb, $6, $7, $8, $9)
            RETURNING *
            """,
            data.sensor_id,
            data.project_id,
            data.version,
            data.kind,
            json.dumps(data.payload),
            data.status.value,
            data.valid_from,
            data.valid_to,
            data.created_by,
        )
        assert record is not None
        return self._to_model(record)

    async def get(self, project_id: UUID, sensor_id: UUID, profile_id: UUID) -> ConversionProfile:
        record = await self._fetchrow(
            """
            SELECT *
            FROM conversion_profiles
            WHERE project_id = $1 AND sensor_id = $2 AND id = $3
            """,
            project_id,
            sensor_id,
            profile_id,
        )
        if record is None:
            raise NotFoundError("Conversion profile not found")
        return self._to_model(record)

    async def update_status(
        self,
        project_id: UUID,
        sensor_id: UUID,
        profile_id: UUID,
        *,
        status: ConversionProfileStatus,
        valid_from,
        valid_to,
        published_by: UUID | None,
    ) -> ConversionProfile:
        record = await self._fetchrow(
            """
            UPDATE conversion_profiles
            SET status = $4,
                valid_from = $5,
                valid_to = $6,
                published_by = $7,
                updated_at = now()
            WHERE project_id = $1 AND sensor_id = $2 AND id = $3
            RETURNING *
            """,
            project_id,
            sensor_id,
            profile_id,
            status.value,
            valid_from,
            valid_to,
            published_by,
        )
        if record is None:
            raise NotFoundError("Conversion profile not found")
        return self._to_model(record)

    async def list_by_sensor(
        self, project_id: UUID, sensor_id: UUID, *, limit: int = 50, offset: int = 0
    ) -> Tuple[List[ConversionProfile], int]:
        records = await self._fetch(
            """
            SELECT *,
                   COUNT(*) OVER() AS total_count
            FROM conversion_profiles
            WHERE project_id = $1 AND sensor_id = $2
            ORDER BY created_at DESC
            LIMIT $3 OFFSET $4
            """,
            project_id,
            sensor_id,
            limit,
            offset,
        )
        items: List[ConversionProfile] = []
        total: int | None = None
        for rec in records:
            rec_dict = dict(rec)
            total_value = rec_dict.pop("total_count", None)
            if total_value is not None:
                total = int(total_value)
            for column in self.JSONB_COLUMNS:
                value = rec_dict.get(column)
                if isinstance(value, str):
                    rec_dict[column] = json.loads(value)
            items.append(ConversionProfile.model_validate(rec_dict))
        if total is None:
            total = await self._count_by_sensor(project_id, sensor_id)
        return items, total

    async def _count_by_sensor(self, project_id: UUID, sensor_id: UUID) -> int:
        record = await self._fetchrow(
            "SELECT COUNT(*) AS total FROM conversion_profiles WHERE project_id = $1 AND sensor_id = $2",
            project_id,
            sensor_id,
        )
        return int(record["total"]) if record else 0

    async def publish_profile(
        self,
        project_id: UUID,
        sensor_id: UUID,
        profile_id: UUID,
        *,
        published_by: UUID,
        effective_from,
    ) -> ConversionProfile:
        async with self._pool.acquire() as conn:
            async with conn.transaction():
                profile = await conn.fetchrow(
                    """
                    SELECT * FROM conversion_profiles
                    WHERE project_id = $1 AND sensor_id = $2 AND id = $3
                    FOR UPDATE
                    """,
                    project_id,
                    sensor_id,
                    profile_id,
                )
                if profile is None:
                    raise NotFoundError("Conversion profile not found")
                current = self._to_model(profile)
                if current.status not in {
                    ConversionProfileStatus.DRAFT,
                    ConversionProfileStatus.SCHEDULED,
                }:
                    raise InvalidStatusTransitionError(
                        "Only draft/scheduled profiles can be published"
                    )
                await conn.execute(
                    """
                    UPDATE conversion_profiles
                    SET status = $4,
                        valid_from = COALESCE($5, now()),
                        published_by = $6,
                        updated_at = now()
                    WHERE project_id = $1 AND sensor_id = $2 AND id = $3
                    """,
                    project_id,
                    sensor_id,
                    profile_id,
                    ConversionProfileStatus.ACTIVE.value,
                    effective_from,
                    published_by,
                )
                await conn.execute(
                    """
                    UPDATE conversion_profiles
                    SET status = $4,
                        valid_to = now(),
                        updated_at = now()
                    WHERE sensor_id = $2
                      AND project_id = $1
                      AND status = $5
                      AND id <> $3
                    """,
                    project_id,
                    sensor_id,
                    profile_id,
                    ConversionProfileStatus.DEPRECATED.value,
                    ConversionProfileStatus.ACTIVE.value,
                )
                await conn.execute(
                    """
                    UPDATE sensors
                    SET active_profile_id = $3,
                        updated_at = now()
                    WHERE project_id = $1 AND id = $2
                    """,
                    project_id,
                    sensor_id,
                    profile_id,
                )
                updated = await conn.fetchrow(
                    """
                    SELECT * FROM conversion_profiles
                    WHERE project_id = $1 AND sensor_id = $2 AND id = $3
                    """,
                    project_id,
                    sensor_id,
                    profile_id,
                )
                assert updated is not None
                return self._to_model(updated)

