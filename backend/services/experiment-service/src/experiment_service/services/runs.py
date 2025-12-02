"""Run domain service."""
from __future__ import annotations

from typing import List
from uuid import UUID

from experiment_service.core.exceptions import ScopeMismatchError
from experiment_service.domain.dto import RunCreateDTO, RunUpdateDTO
from experiment_service.domain.enums import RunStatus
from experiment_service.domain.models import Run
from experiment_service.repositories.experiments import ExperimentRepository
from experiment_service.repositories.runs import RunRepository


class RunService:
    """Coordinates run-related workflows."""

    def __init__(self, repository: RunRepository, experiment_repository: ExperimentRepository):
        self._repository = repository
        self._experiment_repository = experiment_repository

    async def create_run(self, data: RunCreateDTO) -> Run:
        experiment = await self._experiment_repository.get(data.project_id, data.experiment_id)
        if experiment.project_id != data.project_id:
            raise ScopeMismatchError("Experiment does not belong to project")
        return await self._repository.create(data)

    async def get_run(self, project_id: UUID, run_id: UUID) -> Run:
        return await self._repository.get(project_id, run_id)

    async def list_runs(
        self, project_id: UUID, *, limit: int = 50, offset: int = 0
    ) -> tuple[List[Run], int]:
        return await self._repository.list_by_project(project_id, limit=limit, offset=offset)

    async def list_runs_for_experiment(
        self, project_id: UUID, experiment_id: UUID, *, limit: int = 50, offset: int = 0
    ) -> tuple[List[Run], int]:
        return await self._repository.list_by_experiment(
            project_id, experiment_id, limit=limit, offset=offset
        )

    async def update_run(
        self,
        project_id: UUID,
        run_id: UUID,
        updates: RunUpdateDTO,
    ) -> Run:
        return await self._repository.update(project_id, run_id, updates)

    async def delete_run(self, project_id: UUID, run_id: UUID) -> None:
        await self._repository.delete(project_id, run_id)

    async def batch_update_status(
        self, project_id: UUID, run_ids: list[UUID], status: RunStatus
    ) -> List[Run]:
        return await self._repository.update_status_batch(project_id, run_ids, status)

