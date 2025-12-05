"""Sensor domain services."""
from __future__ import annotations

import hashlib
import secrets
from typing import List, Tuple
from uuid import UUID

from experiment_service.domain.dto import (
    ConversionProfileCreateDTO,
    ConversionProfileInputDTO,
    SensorCreateDTO,
    SensorUpdateDTO,
)
from experiment_service.domain.enums import ConversionProfileStatus
from experiment_service.domain.models import ConversionProfile, Sensor
from experiment_service.repositories.conversion_profiles import ConversionProfileRepository
from experiment_service.repositories.sensors import SensorRepository
from experiment_service.services.state_machine import validate_conversion_profile_transition


def generate_sensor_token() -> str:
    return secrets.token_urlsafe(32)


def hash_sensor_token(token: str) -> bytes:
    return hashlib.sha256(token.encode("utf-8")).digest()


class SensorService:
    """Business logic for sensors and their tokens."""

    def __init__(
        self,
        sensor_repository: SensorRepository,
        profile_repository: ConversionProfileRepository,
    ):
        self._sensor_repository = sensor_repository
        self._profile_repository = profile_repository

    async def register_sensor(
        self,
        data: SensorCreateDTO,
        *,
        created_by: UUID,
        initial_profile: ConversionProfileInputDTO | None = None,
    ) -> tuple[Sensor, str]:
        token = generate_sensor_token()
        token_hash = hash_sensor_token(token)
        token_preview = token[-4:]
        sensor = await self._sensor_repository.create(
            data,
            token_hash=token_hash,
            token_preview=token_preview,
        )
        if initial_profile is not None:
            profile_status = initial_profile.status
            if profile_status != ConversionProfileStatus.DRAFT:
                validate_conversion_profile_transition(
                    ConversionProfileStatus.DRAFT,
                    profile_status,
                )
            profile_dto = ConversionProfileCreateDTO(
                sensor_id=sensor.id,
                project_id=sensor.project_id,
                version=initial_profile.version,
                kind=initial_profile.kind,
                payload=initial_profile.payload,
                status=initial_profile.status,
                valid_from=initial_profile.valid_from,
                valid_to=initial_profile.valid_to,
                created_by=created_by,
            )
            profile = await self._profile_repository.create(profile_dto)
            if profile.status == ConversionProfileStatus.ACTIVE:
                await self._sensor_repository.set_active_profile(
                    sensor.project_id,
                    sensor.id,
                    profile.id,
                )
                sensor = await self._sensor_repository.get(sensor.project_id, sensor.id)
        return sensor, token

    async def list_sensors(
        self,
        project_id: UUID,
        *,
        limit: int = 50,
        offset: int = 0,
    ) -> tuple[List[Sensor], int]:
        return await self._sensor_repository.list_by_project(
            project_id,
            limit=limit,
            offset=offset,
        )

    async def get_sensor(self, project_id: UUID, sensor_id: UUID) -> Sensor:
        return await self._sensor_repository.get(project_id, sensor_id)

    async def update_sensor(
        self,
        project_id: UUID,
        sensor_id: UUID,
        updates: SensorUpdateDTO,
    ) -> Sensor:
        return await self._sensor_repository.update(project_id, sensor_id, updates)

    async def delete_sensor(self, project_id: UUID, sensor_id: UUID) -> None:
        await self._sensor_repository.delete(project_id, sensor_id)

    async def rotate_token(
        self,
        project_id: UUID,
        sensor_id: UUID,
    ) -> tuple[Sensor, str]:
        token = generate_sensor_token()
        token_hash = hash_sensor_token(token)
        token_preview = token[-4:]
        sensor = await self._sensor_repository.rotate_token(
            project_id,
            sensor_id,
            token_hash=token_hash,
            token_preview=token_preview,
        )
        return sensor, token


class ConversionProfileService:
    """Operations with conversion profiles."""

    def __init__(
        self,
        profile_repository: ConversionProfileRepository,
        sensor_repository: SensorRepository,
    ):
        self._profile_repository = profile_repository
        self._sensor_repository = sensor_repository

    async def list_profiles(
        self,
        project_id: UUID,
        sensor_id: UUID,
        *,
        limit: int = 50,
        offset: int = 0,
    ) -> tuple[List[ConversionProfile], int]:
        return await self._profile_repository.list_by_sensor(
            project_id,
            sensor_id,
            limit=limit,
            offset=offset,
        )

    async def create_profile(
        self,
        project_id: UUID,
        sensor_id: UUID,
        payload: ConversionProfileInputDTO,
        *,
        created_by: UUID,
    ) -> ConversionProfile:
        _ = await self._sensor_repository.get(project_id, sensor_id)
        profile_status = payload.status
        if profile_status != ConversionProfileStatus.DRAFT:
            validate_conversion_profile_transition(
                ConversionProfileStatus.DRAFT,
                profile_status,
            )
        dto = ConversionProfileCreateDTO(
            sensor_id=sensor_id,
            project_id=project_id,
            version=payload.version,
            kind=payload.kind,
            payload=payload.payload,
            status=payload.status,
            valid_from=payload.valid_from,
            valid_to=payload.valid_to,
            created_by=created_by,
        )
        profile = await self._profile_repository.create(dto)
        if profile.status == ConversionProfileStatus.ACTIVE:
            await self._sensor_repository.set_active_profile(project_id, sensor_id, profile.id)
        return profile

    async def publish_profile(
        self,
        project_id: UUID,
        sensor_id: UUID,
        profile_id: UUID,
        *,
        published_by: UUID,
        effective_from=None,
    ) -> ConversionProfile:
        profile = await self._profile_repository.get(project_id, sensor_id, profile_id)
        if profile.status == ConversionProfileStatus.ACTIVE:
            return profile
        validate_conversion_profile_transition(
            profile.status,
            ConversionProfileStatus.ACTIVE,
        )
        updated = await self._profile_repository.update_status(
            project_id,
            sensor_id,
            profile_id,
            status=ConversionProfileStatus.ACTIVE,
            valid_from=effective_from,
            valid_to=None,
            published_by=published_by,
        )
        await self._sensor_repository.set_active_profile(project_id, sensor_id, profile_id)
        return updated
