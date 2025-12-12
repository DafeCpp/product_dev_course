"""Telemetry ingest service."""
from __future__ import annotations

from datetime import datetime
from typing import Tuple
from uuid import UUID

from experiment_service.domain.dto import (
    SensorUpdateDTO,
    TelemetryIngestDTO,
    TelemetryRecordCreateDTO,
)
from experiment_service.domain.enums import SensorStatus, TelemetryConversionStatus
from experiment_service.domain.models import ConversionProfile, Sensor

from experiment_service.core.exceptions import (
    NotFoundError,
    ScopeMismatchError,
    UnauthorizedError,
)
from experiment_service.repositories import (
    CaptureSessionRepository,
    ConversionProfileRepository,
    RunRepository,
    SensorRepository,
    TelemetryRepository,
)
from experiment_service.services.sensors import hash_sensor_token


class TelemetryService:
    """Validates telemetry ingest payloads and ties them to domain entities."""

    def __init__(
        self,
        sensor_repository: SensorRepository,
        run_repository: RunRepository,
        capture_session_repository: CaptureSessionRepository,
        telemetry_repository: TelemetryRepository,
        profile_repository: ConversionProfileRepository,
    ):
        self._sensor_repository = sensor_repository
        self._run_repository = run_repository
        self._capture_session_repository = capture_session_repository
        self._telemetry_repository = telemetry_repository
        self._profile_repository = profile_repository

    async def ingest(self, payload: TelemetryIngestDTO, *, token: str) -> int:
        """Validate token/scope, convert readings when possible, and persist telemetry."""
        sensor = await self._authenticate_sensor(payload.sensor_id, token)
        project_id = sensor.project_id

        run_id = await self._ensure_run_scope(project_id, payload.run_id)
        capture_run_id = await self._ensure_capture_scope(project_id, payload.capture_session_id)

        if run_id and capture_run_id and run_id != capture_run_id:
            raise ScopeMismatchError("Capture session does not belong to specified run")

        if run_id is None:
            run_id = capture_run_id

        active_profile = await self._get_active_profile(sensor)
        records = []
        for reading in payload.readings:
            conversion_status = TelemetryConversionStatus.RAW_ONLY
            physical_value = reading.physical_value
            conversion_profile_id = None

            if physical_value is None and active_profile is not None:
                physical_value, conversion_status = self._apply_conversion(
                    active_profile, reading.raw_value
                )
                conversion_profile_id = active_profile.id if active_profile else None
            elif physical_value is not None:
                conversion_status = TelemetryConversionStatus.CLIENT_PROVIDED
                conversion_profile_id = active_profile.id if active_profile else None

            record = TelemetryRecordCreateDTO(
                project_id=project_id,
                sensor_id=payload.sensor_id,
                run_id=run_id,
                capture_session_id=payload.capture_session_id,
                timestamp=reading.timestamp,
                raw_value=reading.raw_value,
                physical_value=physical_value,
                meta=reading.meta,
                conversion_status=conversion_status,
                conversion_profile_id=conversion_profile_id,
            )
            records.append(record)

        await self._telemetry_repository.bulk_insert(records)

        last_timestamp = max(reading.timestamp for reading in payload.readings)
        await self._sensor_repository.update(
            project_id,
            payload.sensor_id,
            updates=self._heartbeat_update(last_timestamp),
        )
        return len(payload.readings)

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

    async def _get_active_profile(self, sensor: Sensor) -> ConversionProfile | None:
        if sensor.active_profile_id is None:
            return None
        try:
            return await self._profile_repository.get(
                sensor.project_id,
                sensor.id,
                sensor.active_profile_id,
            )
        except NotFoundError:
            # Sensor references a profile that no longer exists; ingest should still proceed.
            return None

    @staticmethod
    def _apply_conversion(
        profile: ConversionProfile, raw_value: float
    ) -> Tuple[float | None, TelemetryConversionStatus]:
        if profile.kind == "linear":
            payload = profile.payload or {}
            a_raw = payload.get("a")
            b_raw = payload.get("b")
            if not isinstance(a_raw, (int, float)) or not isinstance(b_raw, (int, float)):
                return None, TelemetryConversionStatus.CONVERSION_FAILED
            a = float(a_raw)
            b = float(b_raw)
            return a * raw_value + b, TelemetryConversionStatus.CONVERTED
        return None, TelemetryConversionStatus.CONVERSION_FAILED

    @staticmethod
    def _heartbeat_update(timestamp: datetime) -> SensorUpdateDTO:
        return SensorUpdateDTO(status=SensorStatus.ACTIVE, last_heartbeat=timestamp)

