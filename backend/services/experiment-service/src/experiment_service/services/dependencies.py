"""Shared dependency providers for aiohttp handlers."""
# pyright: reportMissingImports=false
from __future__ import annotations

from dataclasses import dataclass
from typing import Awaitable, Callable, TypeVar
from uuid import UUID

from aiohttp import web

from experiment_service.db.pool import get_pool
from experiment_service.repositories import (
    CaptureSessionRepository,
    ConversionProfileRepository,
    ExperimentRepository,
    RunRepository,
    SensorRepository,
)
from experiment_service.repositories.idempotency import IdempotencyRepository
from experiment_service.services import (
    CaptureSessionService,
    ConversionProfileService,
    ExperimentService,
    RunService,
    TelemetryService,
    SensorService,
)
from experiment_service.services.idempotency import (
    IDEMPOTENCY_HEADER,
    IdempotencyService,
)

TService = TypeVar("TService")

_EXPERIMENT_SERVICE_KEY = "experiment_service"
_RUN_SERVICE_KEY = "run_service"
_CAPTURE_SERVICE_KEY = "capture_session_service"
_IDEMPOTENCY_SERVICE_KEY = "idempotency_service"
_SENSOR_SERVICE_KEY = "sensor_service"
_PROFILE_SERVICE_KEY = "conversion_profile_service"
_TELEMETRY_SERVICE_KEY = "telemetry_service"

USER_ID_HEADER = "X-User-Id"
PROJECT_ID_HEADER = "X-Project-Id"
PROJECT_ROLE_HEADER = "X-Project-Role"


@dataclass
class UserContext:
    user_id: UUID
    project_roles: dict[UUID, str]
    active_project_id: UUID


async def require_current_user(request: web.Request) -> UserContext:
    """Temporary auth hook: relies on debug headers provided by API gateway/tests."""
    user_header = request.headers.get(USER_ID_HEADER)
    project_header = request.headers.get(PROJECT_ID_HEADER)
    if user_header is None or project_header is None:
        raise web.HTTPUnauthorized(
            reason=f"Headers {USER_ID_HEADER} and {PROJECT_ID_HEADER} are required"
        )
    try:
        user_id = UUID(user_header)
    except ValueError as exc:
        raise web.HTTPBadRequest(text=f"Invalid {USER_ID_HEADER}") from exc
    try:
        project_id = UUID(project_header)
    except ValueError as exc:
        raise web.HTTPBadRequest(text=f"Invalid {PROJECT_ID_HEADER}") from exc
    role = request.headers.get(PROJECT_ROLE_HEADER, "owner")
    return UserContext(
        user_id=user_id,
        project_roles={project_id: role},
        active_project_id=project_id,
    )


def ensure_project_access(
    user: UserContext,
    project_id: UUID,
    *,
    require_role: tuple[str, ...] | None = None,
) -> None:
    role = user.project_roles.get(project_id)
    if role is None:
        raise web.HTTPForbidden(reason="User does not belong to project")
    if require_role and role not in require_role:
        raise web.HTTPForbidden(reason="Insufficient project role")


def resolve_project_id(
    user: UserContext,
    project_id_str: str | None,
    *,
    require_role: tuple[str, ...] | None = None,
) -> UUID:
    if project_id_str is None:
        ensure_project_access(user, user.active_project_id, require_role=require_role)
        return user.active_project_id
    try:
        project_id = UUID(project_id_str)
    except ValueError as exc:
        raise web.HTTPBadRequest(text="Invalid project_id") from exc
    ensure_project_access(user, project_id, require_role=require_role)
    return project_id


async def _get_or_create_service(
    request: web.Request,
    cache_key: str,
    builder: Callable[[web.Request], Awaitable[TService]],
) -> TService:
    service = request.get(cache_key)
    if service is None:
        service = await builder(request)
        request[cache_key] = service
    return service


async def get_experiment_service(request: web.Request) -> ExperimentService:
    async def builder(_: web.Request) -> ExperimentService:
        pool = await get_pool()
        repository = ExperimentRepository(pool)
        return ExperimentService(repository)

    return await _get_or_create_service(request, _EXPERIMENT_SERVICE_KEY, builder)


async def get_run_service(request: web.Request) -> RunService:
    async def builder(_: web.Request) -> RunService:
        pool = await get_pool()
        run_repo = RunRepository(pool)
        experiment_repo = ExperimentRepository(pool)
        return RunService(run_repo, experiment_repo)

    return await _get_or_create_service(request, _RUN_SERVICE_KEY, builder)


async def get_capture_session_service(request: web.Request) -> CaptureSessionService:
    async def builder(_: web.Request) -> CaptureSessionService:
        pool = await get_pool()
        run_repo = RunRepository(pool)
        capture_repo = CaptureSessionRepository(pool)
        return CaptureSessionService(capture_repo, run_repo)

    return await _get_or_create_service(request, _CAPTURE_SERVICE_KEY, builder)


async def get_idempotency_service(request: web.Request) -> IdempotencyService:
    async def builder(_: web.Request) -> IdempotencyService:
        pool = await get_pool()
        repo = IdempotencyRepository(pool)
        return IdempotencyService(repo)

    return await _get_or_create_service(request, _IDEMPOTENCY_SERVICE_KEY, builder)


async def get_sensor_service(request: web.Request) -> SensorService:
    async def builder(_: web.Request) -> SensorService:
        pool = await get_pool()
        sensor_repo = SensorRepository(pool)
        profile_repo = ConversionProfileRepository(pool)
        return SensorService(sensor_repo, profile_repo)

    return await _get_or_create_service(request, _SENSOR_SERVICE_KEY, builder)


async def get_conversion_profile_service(request: web.Request) -> ConversionProfileService:
    async def builder(_: web.Request) -> ConversionProfileService:
        pool = await get_pool()
        profile_repo = ConversionProfileRepository(pool)
        sensor_repo = SensorRepository(pool)
        return ConversionProfileService(profile_repo, sensor_repo)

    return await _get_or_create_service(request, _PROFILE_SERVICE_KEY, builder)


async def get_telemetry_service(request: web.Request) -> TelemetryService:
    async def builder(_: web.Request) -> TelemetryService:
        pool = await get_pool()
        sensor_repo = SensorRepository(pool)
        run_repo = RunRepository(pool)
        capture_repo = CaptureSessionRepository(pool)
        return TelemetryService(sensor_repo, run_repo, capture_repo)

    return await _get_or_create_service(request, _TELEMETRY_SERVICE_KEY, builder)
