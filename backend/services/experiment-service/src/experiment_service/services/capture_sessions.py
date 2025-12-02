"""Capture session domain service."""
from __future__ import annotations

from typing import List
from uuid import UUID

from experiment_service.core.exceptions import ScopeMismatchError
from experiment_service.domain.dto import (
    CaptureSessionCreateDTO,
    CaptureSessionUpdateDTO,
)
from experiment_service.domain.models import CaptureSession
from experiment_service.repositories.capture_sessions import CaptureSessionRepository
from experiment_service.repositories.runs import RunRepository


class CaptureSessionService:
    """Business operations for capture sessions."""

    def __init__(self, repository: CaptureSessionRepository, run_repository: RunRepository):
        self._repository = repository
        self._run_repository = run_repository

    async def create_session(self, data: CaptureSessionCreateDTO) -> CaptureSession:
        run = await self._run_repository.get(data.project_id, data.run_id)
        if run.project_id != data.project_id:
            raise ScopeMismatchError("Run does not belong to project")
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
        return await self._repository.update(project_id, capture_session_id, updates)

    async def delete_session(self, project_id: UUID, capture_session_id: UUID) -> None:
        await self._repository.delete(project_id, capture_session_id)

