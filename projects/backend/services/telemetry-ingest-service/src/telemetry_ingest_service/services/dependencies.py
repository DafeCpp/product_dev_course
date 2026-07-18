"""Dependency providers for telemetry routes."""
from __future__ import annotations

from typing import Awaitable, Callable, TypeVar

from aiohttp import web

from backend_common.db.pool import get_pool_service
from telemetry_ingest_service.repositories.sensor_error_log import SensorErrorLogRepository
from telemetry_ingest_service.repositories.telemetry import TelemetryReadRepository
from telemetry_ingest_service.services.auth import UserTokenAuthorizer
from telemetry_ingest_service.services.error_log import SensorErrorLogService
from telemetry_ingest_service.services.telemetry import TelemetryIngestService
from telemetry_ingest_service.services.telemetry_read import TelemetryReadService

T = TypeVar("T")


async def _get_or_create(request: web.Request, key: str, builder: Callable[[], Awaitable[T]]) -> T:
    value = request.get(key)
    if value is None:
        value = await builder()
        request[key] = value
    return value


async def get_ingest_service(request: web.Request) -> TelemetryIngestService:
    async def build() -> TelemetryIngestService:
        return TelemetryIngestService()
    return await _get_or_create(request, "telemetry_ingest_service", build)


async def get_read_service(request: web.Request) -> TelemetryReadService:
    async def build() -> TelemetryReadService:
        pool = await get_pool_service()
        return TelemetryReadService(TelemetryReadRepository(pool), SensorErrorLogRepository(pool))
    return await _get_or_create(request, "telemetry_read_service", build)


async def get_error_log_service(request: web.Request) -> SensorErrorLogService:
    async def build() -> SensorErrorLogService:
        pool = await get_pool_service()
        return SensorErrorLogService(SensorErrorLogRepository(pool))
    return await _get_or_create(request, "sensor_error_log_service", build)


async def get_user_token_authorizer(request: web.Request) -> UserTokenAuthorizer:
    async def build() -> UserTokenAuthorizer:
        return UserTokenAuthorizer()
    return await _get_or_create(request, "user_token_authorizer", build)
