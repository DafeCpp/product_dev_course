"""Bulk endpoint service: collects active configs, computes ETag."""
from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from datetime import datetime
from typing import Any

from config_service.repositories.config_repo import ConfigRepository
from config_service.services.validation_service import ValidationService


@dataclass
class BulkResult:
    configs: dict[str, Any]
    etag: str
    last_modified: datetime | None


class BulkService:
    def __init__(
        self,
        config_repo: ConfigRepository,
        validation_service: ValidationService,
    ) -> None:
        self._config_repo = config_repo
        self._validation = validation_service

    async def get_bulk(
        self,
        service_name: str,
        project_id: str | None,
    ) -> BulkResult:
        configs = await self._config_repo.list_active_by_service(service_name, project_id)

        result: dict[str, Any] = {}
        last_modified: datetime | None = None

        for cfg in configs:
            await self._validation.validate_paranoid(
                cfg.config_type, cfg.value, str(cfg.id)
            )
            result[cfg.key] = cfg.value
            if last_modified is None or cfg.updated_at > last_modified:
                last_modified = cfg.updated_at

        serialized = json.dumps(result, sort_keys=True, separators=(",", ":"), default=str)
        etag = f'W/"{hashlib.sha256(serialized.encode()).hexdigest()[:16]}"'

        return BulkResult(configs=result, etag=etag, last_modified=last_modified)
