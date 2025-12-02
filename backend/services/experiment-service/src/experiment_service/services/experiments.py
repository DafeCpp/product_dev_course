"""Experiment domain service."""
from __future__ import annotations

from typing import List
from uuid import UUID

from experiment_service.domain.dto import (
    ExperimentCreateDTO,
    ExperimentUpdateDTO,
)
from experiment_service.domain.models import Experiment
from experiment_service.repositories.experiments import ExperimentRepository


class ExperimentService:
    """High-level operations for experiments."""

    def __init__(self, repository: ExperimentRepository):
        self._repository = repository

    async def create_experiment(self, data: ExperimentCreateDTO) -> Experiment:
        return await self._repository.create(data)

    async def get_experiment(self, project_id: UUID, experiment_id: UUID) -> Experiment:
        return await self._repository.get(project_id, experiment_id)

    async def list_experiments(
        self, project_id: UUID, *, limit: int = 50, offset: int = 0
    ) -> tuple[List[Experiment], int]:
        return await self._repository.list_by_project(project_id, limit=limit, offset=offset)

    async def update_experiment(
        self,
        project_id: UUID,
        experiment_id: UUID,
        updates: ExperimentUpdateDTO,
    ) -> Experiment:
        return await self._repository.update(project_id, experiment_id, updates)

    async def delete_experiment(self, project_id: UUID, experiment_id: UUID) -> None:
        await self._repository.delete(project_id, experiment_id)

