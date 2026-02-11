"""Capture session domain service."""
from __future__ import annotations

from typing import List
from uuid import UUID

from experiment_service.core.exceptions import InvalidStatusTransitionError
from experiment_service.core.exceptions import ScopeMismatchError
from experiment_service.domain.dto import (
    CaptureSessionCreateDTO,
    CaptureSessionUpdateDTO,
)
from experiment_service.domain.enums import CaptureSessionStatus
from experiment_service.domain.models import CaptureSession
from experiment_service.repositories.capture_sessions import CaptureSessionRepository
from experiment_service.repositories.runs import RunRepository
from experiment_service.repositories.telemetry import TelemetryRepository
from experiment_service.services.state_machine import validate_capture_transition


class CaptureSessionService:
    """Business operations for capture sessions."""

    def __init__(
        self,
        repository: CaptureSessionRepository,
        run_repository: RunRepository,
        telemetry_repository: TelemetryRepository | None = None,
    ):
        self._repository = repository
        self._run_repository = run_repository
        self._telemetry_repository = telemetry_repository

    async def create_session(self, data: CaptureSessionCreateDTO) -> CaptureSession:
        run = await self._run_repository.get(data.project_id, data.run_id)
        if run.project_id != data.project_id:
            raise ScopeMismatchError("Run does not belong to project")
        # Only one active capture session per project (recording window).
        if await self._repository.has_active_for_project(data.project_id):
            raise InvalidStatusTransitionError("Active capture session already exists for this project")
        if data.status != CaptureSessionStatus.DRAFT:
            validate_capture_transition(CaptureSessionStatus.DRAFT, data.status)
        return await self._repository.create(data)

    async def get_session(
        self, project_id: UUID, capture_session_id: UUID
    ) -> CaptureSession:
        return await self._repository.get(project_id, capture_session_id)

    async def list_sessions(
        self, project_id: UUID, *, limit: int = 50, offset: int = 0
    ) -> tuple[List[CaptureSession], int]:
        return await self._repository.list_by_project(project_id, limit=limit, offset=offset)

    async def list_sessions_for_run(
        self, project_id: UUID, run_id: UUID, *, limit: int = 50, offset: int = 0
    ) -> tuple[List[CaptureSession], int]:
        return await self._repository.list_by_run(
            project_id, run_id, limit=limit, offset=offset
        )

    async def update_session(
        self,
        project_id: UUID,
        capture_session_id: UUID,
        updates: CaptureSessionUpdateDTO,
    ) -> CaptureSession:
        current = await self._repository.get(project_id, capture_session_id)
        if updates.status is not None:
            validate_capture_transition(current.status, updates.status)
        return await self._repository.update(project_id, capture_session_id, updates)

    async def delete_session(self, project_id: UUID, capture_session_id: UUID) -> None:
        session = await self._repository.get(project_id, capture_session_id)
        if session.status in (CaptureSessionStatus.RUNNING, CaptureSessionStatus.BACKFILLING):
            raise InvalidStatusTransitionError(
                "Cannot delete capture session while it is active"
            )
        await self._repository.delete(project_id, capture_session_id)

    # ------------------------------------------------------------------
    # Backfill workflow
    # ------------------------------------------------------------------

    async def start_backfill(
        self, project_id: UUID, capture_session_id: UUID
    ) -> CaptureSession:
        """Transition a *succeeded* capture session into ``backfilling``.

        While in ``backfilling`` status the telemetry-ingest-service treats
        incoming data as normal (not late), so the sensor can re-send missing
        readings and they will be attached to the session properly.
        """
        current = await self._repository.get(project_id, capture_session_id)
        validate_capture_transition(current.status, CaptureSessionStatus.BACKFILLING)
        dto = CaptureSessionUpdateDTO(status=CaptureSessionStatus.BACKFILLING)
        return await self._repository.update(project_id, capture_session_id, dto)

    async def complete_backfill(
        self, project_id: UUID, capture_session_id: UUID
    ) -> tuple[CaptureSession, int]:
        """Finish the backfill: attach late records and return to ``succeeded``.

        1. ``UPDATE telemetry_records`` — set ``capture_session_id`` for rows
           that were previously stored as late data (``meta.__system.capture_session_id``).
        2. Transition the session from ``backfilling`` back to ``succeeded``.

        Returns (updated_session, attached_records_count).
        """
        current = await self._repository.get(project_id, capture_session_id)
        if current.status != CaptureSessionStatus.BACKFILLING:
            raise InvalidStatusTransitionError(
                f"Invalid capture session status transition: "
                f"{current.status.value} → {CaptureSessionStatus.SUCCEEDED.value}"
            )

        attached = 0
        if self._telemetry_repository is not None:
            attached = await self._telemetry_repository.attach_late_records(capture_session_id)

        dto = CaptureSessionUpdateDTO(status=CaptureSessionStatus.SUCCEEDED)
        session = await self._repository.update(project_id, capture_session_id, dto)
        return session, attached

