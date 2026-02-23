"""Backfill service for conversion profile reprocessing."""
from __future__ import annotations

from typing import Any
from uuid import UUID

from experiment_service.core.exceptions import NotFoundError
from experiment_service.repositories.backfill_tasks import BackfillTaskRepository
from experiment_service.repositories.conversion_profiles import ConversionProfileRepository
from experiment_service.repositories.sensors import SensorRepository


class BackfillService:
    """Manages conversion backfill task lifecycle."""

    def __init__(
        self,
        backfill_repo: BackfillTaskRepository,
        profile_repo: ConversionProfileRepository,
        sensor_repo: SensorRepository,
    ):
        self._backfill_repo = backfill_repo
        self._profile_repo = profile_repo
        self._sensor_repo = sensor_repo

    async def start_backfill(
        self,
        project_id: UUID,
        sensor_id: UUID,
        *,
        created_by: UUID,
    ) -> dict[str, Any]:
        """Create a backfill task for the sensor's active conversion profile."""
        sensor = await self._sensor_repo.get(project_id, sensor_id)
        if sensor.active_profile_id is None:
            raise NotFoundError("Sensor has no active conversion profile")

        task = await self._backfill_repo.create(
            sensor_id=sensor_id,
            project_id=project_id,
            conversion_profile_id=sensor.active_profile_id,
            created_by=created_by,
        )
        return task

    async def get_task(self, task_id: UUID) -> dict[str, Any]:
        task = await self._backfill_repo.get(task_id)
        if task is None:
            raise NotFoundError("Backfill task not found")
        return task

    async def list_tasks(
        self,
        sensor_id: UUID,
        *,
        limit: int = 20,
        offset: int = 0,
    ) -> tuple[list[dict[str, Any]], int]:
        return await self._backfill_repo.list_by_sensor(
            sensor_id, limit=limit, offset=offset
        )
