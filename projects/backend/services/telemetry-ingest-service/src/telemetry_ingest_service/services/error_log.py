"""Best-effort sensor error logging."""
from __future__ import annotations

import asyncio
from typing import Any

import structlog

from telemetry_ingest_service.repositories.sensor_error_log import SensorErrorLogRepository

logger = structlog.get_logger(__name__)


class SensorErrorLogService:
    def __init__(self, repository: SensorErrorLogRepository) -> None:
        self._repository = repository

    def record_async(self, sensor_id: str, error_code: str, **kwargs: Any) -> None:
        async def record() -> None:
            try:
                await self._repository.insert(sensor_id, error_code, **kwargs)
            except Exception:
                logger.warning("sensor_error_log_write_failed", sensor_id=sensor_id, error_code=error_code)
        asyncio.create_task(record())
