"""Telemetry ingest service."""
from __future__ import annotations

from uuid import UUID

from experiment_service.core.exceptions import (
    NotFoundError,
    ScopeMismatchError,
    UnauthorizedError,
)
from experiment_service.domain.dto import TelemetryIngestDTO
from experiment_service.domain.models import Sensor
from experiment_service.repositories import (
    CaptureSessionRepository,
    RunRepository,
    SensorRepository,
)
from experiment_service.services.sensors import hash_sensor_token


class TelemetryService:
    """Validates telemetry ingest payloads and ties them to domain entities."""

    def __init__(
        self,
        sensor_repository: SensorRepository,
        run_repository: RunRepository,
        capture_session_repository: CaptureSessionRepository,
    ):
        self._sensor_repository = sensor_repository
        self._run_repository = run_repository
        self._capture_session_repository = capture_session_repository

    async def ingest(self, payload: TelemetryIngestDTO, *, token: str) -> None:
        """Validate sensor token + scope and accept payload for future persistence."""
        sensor = await self._authenticate_sensor(payload.sensor_id, token)
        project_id = sensor.project_id

        run_id = await self._ensure_run_scope(project_id, payload.run_id)
        capture_run_id = await self._ensure_capture_scope(
            project_id,
            payload.capture_session_id,
        )

        if run_id and capture_run_id and run_id != capture_run_id:
            raise ScopeMismatchError("Capture session does not belong to specified run")

        # TODO: persist telemetry records + enqueue live stream update.
        # This method currently validates scope and tokens only.

    async def _authenticate_sensor(self, sensor_id: UUID, token: str) -> Sensor:
        token_hash = hash_sensor_token(token)
        try:
            return await self._sensor_repository.get_by_token(sensor_id, token_hash)
        except NotFoundError as exc:  # hide existence detail
            raise UnauthorizedError("Invalid sensor credentials") from exc

    async def _ensure_run_scope(
        self,
        project_id: UUID,
        run_id: UUID | None,
    ) -> UUID | None:
        if run_id is None:
            return None
        run = await self._run_repository.get(project_id, run_id)
        return run.id

    async def _ensure_capture_scope(
        self,
        project_id: UUID,
        capture_session_id: UUID | None,
    ) -> UUID | None:
        if capture_session_id is None:
            return None
        session = await self._capture_session_repository.get(project_id, capture_session_id)
        return session.run_id

