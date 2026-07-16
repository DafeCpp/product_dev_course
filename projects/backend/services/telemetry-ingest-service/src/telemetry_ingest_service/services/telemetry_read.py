"""Read-side telemetry use cases."""
from __future__ import annotations

from datetime import datetime
from typing import Any, Sequence
from uuid import UUID

from telemetry_ingest_service.core.exceptions import NotFoundError, UnauthorizedError
from telemetry_ingest_service.repositories.sensor_error_log import SensorErrorLogRepository
from telemetry_ingest_service.repositories.telemetry import TelemetryReadRepository
from telemetry_ingest_service.services.serializers import serialize_aggregated_record, serialize_telemetry_record
from telemetry_ingest_service.services.telemetry import hash_sensor_token


class TelemetryReadService:
    def __init__(self, repository: TelemetryReadRepository, error_log_repository: SensorErrorLogRepository) -> None:
        self._repository = repository
        self._error_log_repository = error_log_repository

    async def authorize_sensor(self, sensor_id: UUID, token: str, is_user_token: bool) -> UUID:
        project_id = await self._repository.get_sensor_project(
            sensor_id, None if is_user_token else hash_sensor_token(token)
        )
        if project_id is None:
            if is_user_token:
                raise NotFoundError("Sensor not found")
            raise UnauthorizedError("Invalid sensor credentials")
        return project_id

    async def capture_session_project(self, capture_session_id: UUID) -> UUID:
        project_id = await self._repository.get_capture_session_project(capture_session_id)
        if project_id is None:
            raise NotFoundError("Capture session not found")
        return project_id

    async def stream_records(self, project_id: UUID, sensor_id: UUID, since_ts: datetime, since_id: int) -> list[dict[str, Any]]:
        rows = await self._repository.list_stream_records(project_id, sensor_id, since_ts, since_id)
        for row in rows:
            row.setdefault("sensor_id", sensor_id)
            row.setdefault("project_id", project_id)
        return [serialize_telemetry_record(row) for row in rows]

    async def query_records(self, capture_session_id: UUID, sensor_ids: Sequence[UUID], since_id: int, limit: int, include_late: bool, order: str) -> tuple[list[dict[str, Any]], int | None]:
        rows = await self._repository.list_records(capture_session_id, sensor_ids, since_id, limit, include_late, order)
        points = [serialize_telemetry_record(row) for row in rows]
        return points, points[-1]["id"] if len(points) == limit else None

    async def query_aggregated(self, capture_session_id: UUID, sensor_ids: Sequence[UUID], signal: str | None, time_from: datetime | None, time_to: datetime | None, limit: int, order: str) -> list[dict[str, Any]]:
        rows = await self._repository.list_aggregated_records(capture_session_id, sensor_ids, signal, time_from, time_to, limit, order)
        return [serialize_aggregated_record(row) for row in rows]

    async def sensor_error_entries(self, sensor_id: UUID, limit: int, offset: int) -> tuple[list[Any], int]:
        entries = await self._error_log_repository.list_for_sensor(str(sensor_id), limit=limit, offset=offset)
        total = await self._error_log_repository.count_for_sensor(str(sensor_id))
        return entries, total
