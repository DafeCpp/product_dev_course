"""Run domain service."""
from __future__ import annotations

from datetime import datetime
from typing import List
from uuid import UUID

from experiment_service.core.exceptions import InvalidStatusTransitionError
from experiment_service.core.exceptions import ScopeMismatchError
from experiment_service.domain.dto import RunCreateDTO, RunUpdateDTO
from experiment_service.domain.enums import RunStatus
from experiment_service.domain.models import Run
from experiment_service.repositories.capture_sessions import CaptureSessionRepository
from experiment_service.repositories.experiments import ExperimentRepository
from experiment_service.repositories.runs import RunRepository
from experiment_service.services.state_machine import validate_run_transition


class RunService:
    """Coordinates run-related workflows."""

    def __init__(
        self,
        repository: RunRepository,
        experiment_repository: ExperimentRepository,
        capture_session_repository: CaptureSessionRepository,
    ):
        self._repository = repository
        self._experiment_repository = experiment_repository
        self._capture_session_repository = capture_session_repository

    async def _ensure_no_active_capture_sessions(self, project_id: UUID, run_id: UUID) -> None:
        has_active = await self._capture_session_repository.has_active_for_run(project_id, run_id)
        if has_active:
            raise InvalidStatusTransitionError(
                "Cannot finish or archive run while capture sessions are active"
            )

    async def create_run(self, data: RunCreateDTO) -> Run:
        experiment = await self._experiment_repository.get(data.project_id, data.experiment_id)
        if experiment.project_id != data.project_id:
            raise ScopeMismatchError("Experiment does not belong to project")
        if data.status != RunStatus.DRAFT:
            validate_run_transition(RunStatus.DRAFT, data.status)
        return await self._repository.create(data)

    async def get_run(self, project_id: UUID, run_id: UUID) -> Run:
        return await self._repository.get(project_id, run_id)

    async def list_runs(
        self, project_id: UUID, *, limit: int = 50, offset: int = 0
    ) -> tuple[List[Run], int]:
        return await self._repository.list_by_project(project_id, limit=limit, offset=offset)

    async def list_runs_for_experiment(
        self,
        project_id: UUID,
        experiment_id: UUID,
        *,
        limit: int = 50,
        offset: int = 0,
        status: RunStatus | None = None,
        tags: list[str] | None = None,
        created_after: datetime | None = None,
        created_before: datetime | None = None,
    ) -> tuple[List[Run], int]:
        return await self._repository.list_by_experiment(
            project_id,
            experiment_id,
            limit=limit,
            offset=offset,
            status=status,
            tags=tags,
            created_after=created_after,
            created_before=created_before,
        )

    async def update_run(
        self,
        project_id: UUID,
        run_id: UUID,
        updates: RunUpdateDTO,
    ) -> Run:
        current = await self._repository.get(project_id, run_id)
        if updates.status is not None:
            validate_run_transition(current.status, updates.status)
            if updates.status in (RunStatus.SUCCEEDED, RunStatus.FAILED, RunStatus.ARCHIVED):
                await self._ensure_no_active_capture_sessions(project_id, run_id)
        return await self._repository.update(project_id, run_id, updates)

    async def delete_run(self, project_id: UUID, run_id: UUID) -> None:
        await self._repository.delete(project_id, run_id)

    async def batch_update_status(
        self, project_id: UUID, run_ids: list[UUID], status: RunStatus
    ) -> List[Run]:
        runs = []
        for run_id in run_ids:
            current = await self._repository.get(project_id, run_id)
            validate_run_transition(current.status, status)
            if status in (RunStatus.SUCCEEDED, RunStatus.FAILED, RunStatus.ARCHIVED):
                await self._ensure_no_active_capture_sessions(project_id, run_id)
            runs.append(current)
        return await self._repository.update_status_batch(project_id, run_ids, status)

    async def bulk_update_tags(
        self,
        project_id: UUID,
        run_ids: list[UUID],
        *,
        add_tags: list[str] | None = None,
        remove_tags: list[str] | None = None,
        set_tags: list[str] | None = None,
    ) -> List[Run]:
        return await self._repository.bulk_update_tags(
            project_id,
            run_ids,
            add_tags=add_tags,
            remove_tags=remove_tags,
            set_tags=set_tags,
        )