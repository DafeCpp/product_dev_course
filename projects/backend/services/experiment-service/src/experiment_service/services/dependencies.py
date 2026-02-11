"""Shared dependency providers for aiohttp handlers."""
# pyright: reportMissingImports=false
from __future__ import annotations

from dataclasses import dataclass
from typing import Awaitable, Callable, TypeVar
from uuid import UUID

from aiohttp import web

from backend_common.db.pool import get_pool_service as get_pool
from experiment_service.repositories import (
    CaptureSessionRepository,
    CaptureSessionEventRepository,
    ConversionProfileRepository,
    ExperimentRepository,
    RunMetricsRepository,
    RunEventRepository,
    RunRepository,
    SensorRepository,
    TelemetryRepository,
)
from experiment_service.repositories.webhooks import WebhookDeliveryRepository, WebhookSubscriptionRepository
from experiment_service.repositories.idempotency import IdempotencyRepository
from experiment_service.services import (
    CaptureSessionService,
    CaptureSessionEventService,
    ConversionProfileService,
    ExperimentService,
    MetricsService,
    RunEventService,
    RunService,
    SensorService,
    TelemetryService,
    WebhookService,
)
from experiment_service.services.idempotency import (
    IDEMPOTENCY_HEADER,
    IdempotencyService,
)

TService = TypeVar("TService")

_EXPERIMENT_SERVICE_KEY = "experiment_service"
_RUN_SERVICE_KEY = "run_service"
_RUN_EVENT_SERVICE_KEY = "run_event_service"
_CAPTURE_SERVICE_KEY = "capture_session_service"
_CAPTURE_EVENT_SERVICE_KEY = "capture_session_event_service"
_WEBHOOK_SERVICE_KEY = "webhook_service"
_IDEMPOTENCY_SERVICE_KEY = "idempotency_service"
_SENSOR_SERVICE_KEY = "sensor_service"
_PROFILE_SERVICE_KEY = "conversion_profile_service"
_TELEMETRY_SERVICE_KEY = "telemetry_service"
_METRICS_SERVICE_KEY = "metrics_service"

USER_ID_HEADER = "X-User-Id"
PROJECT_ID_HEADER = "X-Project-Id"
PROJECT_ROLE_HEADER = "X-Project-Role"


@dataclass
class UserContext:
    user_id: UUID
    project_roles: dict[UUID, str]
    active_project_id: UUID | None


async def require_current_user(request: web.Request) -> UserContext:
    """Temporary auth hook: relies on debug headers provided by API gateway/tests."""
    user_header = request.headers.get(USER_ID_HEADER)
    if user_header is None:
        raise web.HTTPUnauthorized(
            reason=f"Header {USER_ID_HEADER} is required"
        )
    try:
        user_id = UUID(user_header)
    except ValueError as exc:
        raise web.HTTPBadRequest(text=f"Invalid {USER_ID_HEADER}") from exc

    # project_id опционален - если не передан, будет использован из query/body или вернется ошибка
    project_header = request.headers.get(PROJECT_ID_HEADER)
    project_id: UUID | None = None
    if project_header:
        try:
            project_id = UUID(project_header)
        except ValueError as exc:
            raise web.HTTPBadRequest(text=f"Invalid {PROJECT_ID_HEADER}") from exc

    # Получаем роль из заголовка, если она установлена
    # Если заголовок не установлен, role будет None, и мы не добавим проект в project_roles
    # Это позволяет experiment-service проверить доступ через ensure_project_access
    role = request.headers.get(PROJECT_ROLE_HEADER)

    # Если project_id передан, используем его как active_project_id
    # Если не передан, active_project_id будет None (будет установлен позже через resolve_project_id)
    # Добавляем проект в project_roles только если роль была установлена
    return UserContext(
        user_id=user_id,
        project_roles={project_id: role} if (project_id and role) else {},
        active_project_id=project_id,  # Может быть None
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
        # Если project_id не передан, используем active_project_id из заголовков
        if user.active_project_id is None:
            raise web.HTTPBadRequest(
                text="project_id is required. Provide it in query parameter, request body, or X-Project-Id header"
            )
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
        capture_repo = CaptureSessionRepository(pool)
        return RunService(run_repo, experiment_repo, capture_repo)

    return await _get_or_create_service(request, _RUN_SERVICE_KEY, builder)


async def get_run_event_service(request: web.Request) -> RunEventService:
    async def builder(_: web.Request) -> RunEventService:
        pool = await get_pool()
        repo = RunEventRepository(pool)
        return RunEventService(repo)

    return await _get_or_create_service(request, _RUN_EVENT_SERVICE_KEY, builder)


async def get_capture_session_service(request: web.Request) -> CaptureSessionService:
    async def builder(_: web.Request) -> CaptureSessionService:
        pool = await get_pool()
        run_repo = RunRepository(pool)
        capture_repo = CaptureSessionRepository(pool)
        telemetry_repo = TelemetryRepository(pool)
        return CaptureSessionService(capture_repo, run_repo, telemetry_repo)

    return await _get_or_create_service(request, _CAPTURE_SERVICE_KEY, builder)


async def get_capture_session_event_service(request: web.Request) -> CaptureSessionEventService:
    async def builder(_: web.Request) -> CaptureSessionEventService:
        pool = await get_pool()
        repo = CaptureSessionEventRepository(pool)
        return CaptureSessionEventService(repo)

    return await _get_or_create_service(request, _CAPTURE_EVENT_SERVICE_KEY, builder)


async def get_webhook_service(request: web.Request) -> WebhookService:
    async def builder(_: web.Request) -> WebhookService:
        pool = await get_pool()
        subs_repo = WebhookSubscriptionRepository(pool)
        deliveries_repo = WebhookDeliveryRepository(pool)
        return WebhookService(subs_repo, deliveries_repo)

    return await _get_or_create_service(request, _WEBHOOK_SERVICE_KEY, builder)


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
        telemetry_repo = TelemetryRepository(pool)
        profile_repo = ConversionProfileRepository(pool)
        return TelemetryService(sensor_repo, run_repo, capture_repo, telemetry_repo, profile_repo)

    return await _get_or_create_service(request, _TELEMETRY_SERVICE_KEY, builder)


async def get_metrics_service(request: web.Request) -> MetricsService:
    async def builder(_: web.Request) -> MetricsService:
        pool = await get_pool()
        run_repo = RunRepository(pool)
        metrics_repo = RunMetricsRepository(pool)
        return MetricsService(run_repo, metrics_repo)

    return await _get_or_create_service(request, _METRICS_SERVICE_KEY, builder)
