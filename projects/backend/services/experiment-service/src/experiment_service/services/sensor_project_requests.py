"""Attaching sensors to other projects through approved requests."""
from __future__ import annotations

from typing import List, Literal
from uuid import UUID

from backend_common.core.exceptions import ConflictError
from experiment_service.core.exceptions import NotFoundError
from experiment_service.domain.enums import SensorProjectRequestStatus
from experiment_service.domain.models import SensorProjectRequest
from experiment_service.repositories.sensor_project_requests import (
    SensorProjectRequestRepository,
)
from experiment_service.repositories.sensors import SensorRepository

Direction = Literal["incoming", "outgoing"]


class SensorProjectRequestService:
    """A sensor joins another project only after a member of a project it
    already belongs to approves the request."""

    def __init__(
        self,
        request_repository: SensorProjectRequestRepository,
        sensor_repository: SensorRepository,
    ):
        self._requests = request_repository
        self._sensors = sensor_repository

    async def request_attach(
        self, sensor_id: UUID, project_id: UUID, requested_by: UUID
    ) -> SensorProjectRequest:
        """Open a request to attach ``sensor_id`` to ``project_id``."""
        await self._sensors.get_by_id(sensor_id)
        if await self._requests.sensor_in_project(sensor_id, project_id):
            raise ConflictError("Sensor already belongs to this project")
        return await self._requests.create_or_get_pending(sensor_id, project_id, requested_by)

    async def list_requests(
        self,
        project_id: UUID,
        direction: Direction,
        status: SensorProjectRequestStatus | None,
    ) -> List[SensorProjectRequest]:
        if direction == "incoming":
            return await self._requests.list_incoming(project_id, status)
        return await self._requests.list_outgoing(project_id, status)

    async def decide(
        self,
        request_id: UUID,
        project_id: UUID,
        user_id: UUID,
        status: SensorProjectRequestStatus,
    ) -> SensorProjectRequest:
        """Approve or reject from ``project_id``, which must already own the sensor."""
        request = await self._requests.get(request_id)
        # Requests for sensors outside the caller's project are reported as
        # missing, so their existence is not disclosed.
        if request.project_id == project_id or not await self._requests.sensor_in_project(
            request.sensor_id, project_id
        ):
            raise NotFoundError("Sensor project request not found")
        return await self._finish(request, project_id, user_id, status)

    async def cancel(
        self, request_id: UUID, project_id: UUID, user_id: UUID
    ) -> SensorProjectRequest:
        """Withdraw a request from the project that made it."""
        request = await self._requests.get(request_id)
        if request.project_id != project_id:
            raise NotFoundError("Sensor project request not found")
        return await self._finish(
            request, project_id, user_id, SensorProjectRequestStatus.CANCELLED
        )

    async def _finish(
        self,
        request: SensorProjectRequest,
        project_id: UUID,
        user_id: UUID,
        status: SensorProjectRequestStatus,
    ) -> SensorProjectRequest:
        if request.status is not SensorProjectRequestStatus.PENDING:
            raise ConflictError(f"Request is already {request.status.value}")
        decided = await self._requests.decide(request.id, status, user_id, project_id)
        if decided is None:
            raise ConflictError("Request is no longer pending")
        return decided
