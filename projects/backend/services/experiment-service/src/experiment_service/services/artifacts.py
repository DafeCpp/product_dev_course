"""Artifact domain service."""
from __future__ import annotations

from typing import Any
from uuid import UUID

from experiment_service.core.exceptions import NotFoundError
from experiment_service.domain.models import Artifact
from experiment_service.repositories.artifacts import ArtifactRepository
from experiment_service.repositories.runs import RunRepository


class ArtifactService:
    """Coordinates artifact-related workflows."""

    def __init__(
        self,
        repository: ArtifactRepository,
        run_repository: RunRepository,
    ) -> None:
        self._repository = repository
        self._run_repository = run_repository

    async def _ensure_run(self, project_id: UUID, run_id: UUID) -> None:
        await self._run_repository.get(project_id, run_id)

    async def create_artifact(
        self,
        *,
        project_id: UUID,
        run_id: UUID,
        type: str,
        uri: str,
        created_by: UUID,
        checksum: str | None = None,
        size_bytes: int | None = None,
        metadata: dict[str, Any] | None = None,
        is_restricted: bool = False,
    ) -> Artifact:
        await self._ensure_run(project_id, run_id)
        return await self._repository.create(
            run_id=run_id,
            project_id=project_id,
            type=type,
            uri=uri,
            created_by=created_by,
            checksum=checksum,
            size_bytes=size_bytes,
            metadata=metadata,
            is_restricted=is_restricted,
        )

    async def get_artifact(self, artifact_id: UUID) -> Artifact:
        artifact = await self._repository.get(artifact_id)
        if artifact is None:
            raise NotFoundError("Artifact not found")
        return artifact

    async def list_artifacts_by_run(
        self,
        run_id: UUID,
        *,
        type_filter: str | None = None,
        limit: int = 50,
        offset: int = 0,
    ) -> tuple[list[Artifact], int]:
        return await self._repository.list_by_run(
            run_id,
            type_filter=type_filter,
            limit=limit,
            offset=offset,
        )

    async def delete_artifact(self, artifact_id: UUID) -> None:
        deleted = await self._repository.delete(artifact_id)
        if not deleted:
            raise NotFoundError("Artifact not found")

    async def approve_artifact(
        self,
        artifact_id: UUID,
        user_id: UUID,
        note: str | None = None,
    ) -> Artifact:
        artifact = await self._repository.approve(artifact_id, user_id, note)
        if artifact is None:
            raise NotFoundError("Artifact not found")
        return artifact
