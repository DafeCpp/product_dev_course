"""Experiment domain service."""
from __future__ import annotations

from datetime import datetime
from typing import List
from uuid import UUID

from experiment_service.domain.dto import (
    ExperimentCreateDTO,
    ExperimentUpdateDTO,
)
from experiment_service.domain.enums import ExperimentStatus
from experiment_service.domain.models import Experiment
from experiment_service.repositories.experiments import ExperimentRepository
from experiment_service.services.state_machine import validate_experiment_transition


class ExperimentService:
    """High-level operations for experiments."""

    def __init__(self, repository: ExperimentRepository):
        self._repository = repository

    async def create_experiment(self, data: ExperimentCreateDTO) -> Experiment:
        if data.status != ExperimentStatus.DRAFT:
            validate_experiment_transition(ExperimentStatus.DRAFT, data.status)
        return await self._repository.create(data)

    async def get_experiment(self, project_id: UUID, experiment_id: UUID) -> Experiment:
        return await self._repository.get(project_id, experiment_id)

    async def list_experiments(
        self,
        project_id: UUID,
        *,
        limit: int = 50,
        offset: int = 0,
        status: ExperimentStatus | None = None,
        tags: list[str] | None = None,
        created_after: datetime | None = None,
        created_before: datetime | None = None,
    ) -> tuple[List[Experiment], int]:
        return await self._repository.list_by_project(
            project_id,
            limit=limit,
            offset=offset,
            status=status,
            tags=tags,
            created_after=created_after,
            created_before=created_before,
        )

    async def search_experiments(
        self,
        project_id: UUID,
        query: str,
        *,
        limit: int = 50,
        offset: int = 0,
    ) -> tuple[List[Experiment], int]:
        """Search experiments by name and description."""
        return await self._repository.search_experiments(
            project_id, query, limit=limit, offset=offset
        )

    async def update_experiment(
        self,
        project_id: UUID,
        experiment_id: UUID,
        updates: ExperimentUpdateDTO,
    ) -> Experiment:
        current = await self._repository.get(project_id, experiment_id)
        if updates.status is not None:
            validate_experiment_transition(current.status, updates.status)
        return await self._repository.update(project_id, experiment_id, updates)

    async def delete_experiment(self, project_id: UUID, experiment_id: UUID) -> None:
        await self._repository.delete(project_id, experiment_id)
