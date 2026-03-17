"""Script management service."""
from __future__ import annotations

from typing import Any
from uuid import UUID

import structlog

from script_service.domain.models import Script, ScriptType
from script_service.repositories.scripts import ScriptRepository

logger = structlog.get_logger(__name__)


class ScriptNotFoundError(Exception):
    """Raised when script is not found."""


class ScriptManager:
    """High-level operations for script lifecycle management."""

    def __init__(self, script_repo: ScriptRepository) -> None:
        self._repo = script_repo

    async def create_script(
        self,
        name: str,
        description: str | None,
        target_service: str,
        script_type: ScriptType,
        script_body: str,
        parameters_schema: dict[str, Any],
        timeout_sec: int,
        created_by: UUID,
    ) -> Script:
        script = await self._repo.create(
            name=name,
            description=description,
            target_service=target_service,
            script_type=script_type,
            script_body=script_body,
            parameters_schema=parameters_schema,
            timeout_sec=timeout_sec,
            created_by=created_by,
        )
        logger.info("script_created", script_id=str(script.id), name=name)
        return script

    async def get_script(self, id: UUID) -> Script:
        script = await self._repo.get_by_id(id)
        if script is None:
            raise ScriptNotFoundError(f"Script {id} not found")
        return script

    async def list_scripts(
        self,
        *,
        target_service: str | None = None,
        is_active: bool | None = None,
        limit: int = 50,
        offset: int = 0,
    ) -> list[Script]:
        return await self._repo.list(
            target_service=target_service,
            is_active=is_active,
            limit=limit,
            offset=offset,
        )

    async def update_script(self, id: UUID, **fields: Any) -> Script:
        script = await self._repo.update(id, **fields)
        if script is None:
            raise ScriptNotFoundError(f"Script {id} not found")
        logger.info("script_updated", script_id=str(id))
        return script

    async def delete_script(self, id: UUID) -> None:
        """Soft-delete: sets is_active = False."""
        found = await self._repo.soft_delete(id)
        if not found:
            raise ScriptNotFoundError(f"Script {id} not found")
        logger.info("script_soft_deleted", script_id=str(id))
