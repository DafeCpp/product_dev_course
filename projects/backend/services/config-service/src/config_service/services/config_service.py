"""Core config business logic: CRUD, rollback, dry-run, optimistic locking."""
from __future__ import annotations

from typing import Any
from uuid import UUID

import structlog

from config_service.core.exceptions import (
    ConfigNotFoundError,
    VersionConflictError,
)
from config_service.domain.models import Config, ConfigHistory
from config_service.repositories.config_repo import ConfigRepository
from config_service.repositories.history_repo import HistoryRepository
from config_service.services.validation_service import ValidationService

logger = structlog.get_logger(__name__)


class ConfigService:
    def __init__(
        self,
        config_repo: ConfigRepository,
        history_repo: HistoryRepository,
        validation_service: ValidationService,
    ) -> None:
        self._config_repo = config_repo
        self._history_repo = history_repo
        self._validation = validation_service

    async def create(
        self,
        *,
        service_name: str,
        project_id: str | None,
        key: str,
        config_type: Any,
        description: str | None,
        value: dict[str, Any],
        metadata: dict[str, Any],
        is_critical: bool,
        is_sensitive: bool,
        created_by: str,
        change_reason: str | None,
        source_ip: str | None,
        user_agent: str | None,
        correlation_id: str | None,
    ) -> Config:
        await self._validation.validate_strict(config_type, value)
        return await self._config_repo.create(
            service_name=service_name,
            project_id=project_id,
            key=key,
            config_type=config_type,
            description=description,
            value=value,
            metadata=metadata,
            is_critical=is_critical,
            is_sensitive=is_sensitive,
            created_by=created_by,
            change_reason=change_reason or "Initial creation",
            source_ip=source_ip,
            user_agent=user_agent,
            correlation_id=correlation_id,
        )

    async def get(self, config_id: UUID) -> Config:
        config = await self._config_repo.get_by_id(config_id)
        if config is None:
            raise ConfigNotFoundError(str(config_id))
        await self._validation.validate_paranoid(
            config.config_type, config.value, str(config_id)
        )
        return config

    async def list_configs(
        self,
        *,
        service_name: str | None = None,
        project_id: str | None = None,
        config_type: Any = None,
        is_active: bool | None = None,
        limit: int = 50,
        cursor: str | None = None,
    ) -> tuple[list[Config], str | None]:
        return await self._config_repo.list_by_filters(
            service_name=service_name,
            project_id=project_id,
            config_type=config_type,
            is_active=is_active,
            limit=limit,
            cursor=cursor,
        )

    async def patch(
        self,
        config_id: UUID,
        expected_version: int,
        *,
        changed_by: str,
        change_reason: str,
        source_ip: str | None,
        user_agent: str | None,
        correlation_id: str | None,
        description: str | None = None,
        value: dict[str, Any] | None = None,
        metadata: dict[str, Any] | None = None,
        is_active: bool | None = None,
        is_critical: bool | None = None,
        is_sensitive: bool | None = None,
    ) -> Config:
        if value is not None:
            existing = await self._config_repo.get_by_id(config_id)
            if existing is None:
                raise ConfigNotFoundError(str(config_id))
            await self._validation.validate_strict(existing.config_type, value)

        result = await self._config_repo.update_with_version(
            config_id=config_id,
            expected_version=expected_version,
            changed_by=changed_by,
            change_reason=change_reason,
            source_ip=source_ip,
            user_agent=user_agent,
            correlation_id=correlation_id,
            description=description,
            value=value,
            metadata=metadata,
            is_active=is_active,
            is_critical=is_critical,
            is_sensitive=is_sensitive,
        )
        if result is None:
            raise VersionConflictError(str(config_id), expected_version, -1)
        return result

    async def soft_delete(
        self,
        config_id: UUID,
        expected_version: int,
        *,
        deleted_by: str,
        change_reason: str,
        source_ip: str | None,
        user_agent: str | None,
        correlation_id: str | None,
    ) -> Config:
        result = await self._config_repo.soft_delete(
            config_id=config_id,
            expected_version=expected_version,
            deleted_by=deleted_by,
            change_reason=change_reason,
            source_ip=source_ip,
            user_agent=user_agent,
            correlation_id=correlation_id,
        )
        if result is None:
            config = await self._config_repo.get_by_id(config_id)
            if config is None:
                raise ConfigNotFoundError(str(config_id))
            raise VersionConflictError(str(config_id), expected_version, config.version)
        return result

    async def rollback(
        self,
        config_id: UUID,
        expected_version: int,
        target_version: int,
        *,
        changed_by: str,
        change_reason: str,
        source_ip: str | None,
        user_agent: str | None,
        correlation_id: str | None,
    ) -> Config:
        target = await self._history_repo.get_by_version(config_id, target_version)
        if target is None:
            raise ConfigNotFoundError(f"{config_id} history version {target_version}")

        await self._validation.validate_strict(target.config_type, target.value)

        result = await self._config_repo.update_with_version(
            config_id=config_id,
            expected_version=expected_version,
            changed_by=changed_by,
            change_reason=change_reason,
            source_ip=source_ip,
            user_agent=user_agent,
            correlation_id=correlation_id,
            value=target.value,
            metadata=target.metadata,
            is_active=target.is_active,
        )
        if result is None:
            raise VersionConflictError(str(config_id), expected_version, -1)
        return result

    async def get_history(
        self,
        config_id: UUID,
        limit: int = 50,
        offset: int = 0,
    ) -> list[ConfigHistory]:
        config = await self._config_repo.get_by_id(config_id)
        if config is None:
            raise ConfigNotFoundError(str(config_id))
        return await self._history_repo.list_by_config_id(config_id, limit, offset)
