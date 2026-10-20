"""Repository for requests to attach a sensor to another project."""
from __future__ import annotations

from typing import List
from uuid import UUID

from asyncpg import Pool, Record  # type: ignore[import-untyped]

from backend_common.repositories.base import BaseRepository
from experiment_service.core.exceptions import NotFoundError
from experiment_service.domain.enums import SensorProjectRequestStatus
from experiment_service.domain.models import SensorProjectRequest

_COLUMNS = (
    "r.id, r.sensor_id, r.project_id, r.requested_by, r.status, r.decided_by, "
    "r.decided_in_project_id, r.created_at, r.decided_at"
)

# A sensor belongs to a project through sensor_projects or, for sensors created
# before the many-to-many table existed, through sensors.project_id.
_SENSOR_IN_PROJECT = """
    (
        EXISTS (
            SELECT 1 FROM sensor_projects sp
            WHERE sp.sensor_id = {sensor} AND sp.project_id = {project}
        )
        OR EXISTS (
            SELECT 1 FROM sensors s0
            WHERE s0.id = {sensor} AND s0.project_id = {project}
        )
    )
"""


class SensorProjectRequestRepository(BaseRepository):
    """Persistence for sensor ↔ project attach requests."""

    def __init__(self, pool: Pool):
        super().__init__(pool)

    @staticmethod
    def _to_model(record: Record) -> SensorProjectRequest:
        return SensorProjectRequest.model_validate(dict(record))

    async def sensor_in_project(self, sensor_id: UUID, project_id: UUID) -> bool:
        record = await self._fetchrow(
            "SELECT " + _SENSOR_IN_PROJECT.format(sensor="$1", project="$2") + " AS ok",
            sensor_id,
            project_id,
        )
        return bool(record and record["ok"])

    async def create_or_get_pending(
        self, sensor_id: UUID, project_id: UUID, requested_by: UUID
    ) -> SensorProjectRequest:
        """Create a pending request, or return the one already open for this pair."""
        async with self._pool.acquire() as conn:
            record = await conn.fetchrow(
                f"""
                INSERT INTO sensor_project_requests AS r (sensor_id, project_id, requested_by)
                VALUES ($1, $2, $3)
                ON CONFLICT (sensor_id, project_id) WHERE status = 'pending' DO NOTHING
                RETURNING {_COLUMNS}
                """,
                sensor_id,
                project_id,
                requested_by,
            )
            if record is None:
                record = await conn.fetchrow(
                    f"""
                    SELECT {_COLUMNS} FROM sensor_project_requests r
                    WHERE r.sensor_id = $1 AND r.project_id = $2 AND r.status = 'pending'
                    """,
                    sensor_id,
                    project_id,
                )
        if record is None:
            # The concurrent pending request was decided between the two statements.
            raise NotFoundError("Sensor project request not found")
        return self._to_model(record)

    async def get(self, request_id: UUID) -> SensorProjectRequest:
        record = await self._fetchrow(
            f"SELECT {_COLUMNS} FROM sensor_project_requests r WHERE r.id = $1",
            request_id,
        )
        if record is None:
            raise NotFoundError("Sensor project request not found")
        return self._to_model(record)

    async def list_incoming(
        self, project_id: UUID, status: SensorProjectRequestStatus | None
    ) -> List[SensorProjectRequest]:
        """Requests for sensors that already belong to ``project_id``."""
        records = await self._fetch(
            f"""
            SELECT {_COLUMNS}, s.name AS sensor_name
            FROM sensor_project_requests r
            JOIN sensors s ON s.id = r.sensor_id
            WHERE {_SENSOR_IN_PROJECT.format(sensor="r.sensor_id", project="$1")}
              AND ($2::text IS NULL OR r.status = $2)
            ORDER BY r.created_at DESC
            """,
            project_id,
            status.value if status else None,
        )
        return [self._to_model(r) for r in records]

    async def list_outgoing(
        self, project_id: UUID, status: SensorProjectRequestStatus | None
    ) -> List[SensorProjectRequest]:
        """Requests made to attach sensors to ``project_id``."""
        records = await self._fetch(
            f"""
            SELECT {_COLUMNS}
            FROM sensor_project_requests r
            WHERE r.project_id = $1
              AND ($2::text IS NULL OR r.status = $2)
            ORDER BY r.created_at DESC
            """,
            project_id,
            status.value if status else None,
        )
        return [self._to_model(r) for r in records]

    async def decide(
        self,
        request_id: UUID,
        status: SensorProjectRequestStatus,
        decided_by: UUID,
        decided_in_project_id: UUID,
    ) -> SensorProjectRequest | None:
        """Move a pending request to ``status``; on approval attach the sensor.

        Returns None if the request is no longer pending.
        """
        async with self._pool.acquire() as conn:
            async with conn.transaction():
                record = await conn.fetchrow(
                    f"""
                    UPDATE sensor_project_requests AS r
                    SET status = $2, decided_by = $3, decided_in_project_id = $4,
                        decided_at = now()
                    WHERE r.id = $1 AND r.status = 'pending'
                    RETURNING {_COLUMNS}
                    """,
                    request_id,
                    status.value,
                    decided_by,
                    decided_in_project_id,
                )
                if record is None:
                    return None
                if status is SensorProjectRequestStatus.APPROVED:
                    await conn.execute(
                        """
                        INSERT INTO sensor_projects (sensor_id, project_id)
                        VALUES ($1, $2)
                        ON CONFLICT (sensor_id, project_id) DO NOTHING
                        """,
                        record["sensor_id"],
                        record["project_id"],
                    )
        return self._to_model(record)
