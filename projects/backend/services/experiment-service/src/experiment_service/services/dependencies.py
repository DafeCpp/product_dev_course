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
from experiment_service.repositories.artifacts import ArtifactRepository
from experiment_service.repositories.backfill_tasks import BackfillTaskRepository
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
from experiment_service.services.artifacts import ArtifactService
from experiment_service.services.backfill import BackfillService
from experiment_service.services.idempotency import IdempotencyService

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
_BACKFILL_SERVICE_KEY = "backfill_service"
_ARTIFACT_SERVICE_KEY = "artifact_service"

USER_ID_HEADER = "X-User-Id"
PROJECT_ID_HEADER = "X-Project-Id"
IS_SUPERADMIN_HEADER = "X-User-Is-Superadmin"
SYSTEM_PERMISSIONS_HEADER = "X-User-System-Permissions"
PROJECT_PERMISSIONS_HEADER = "X-User-Permissions"


@dataclass
class UserContext:
    user_id: UUID
    is_superadmin: bool
    system_permissions: frozenset[str]
    project_permissions: frozenset[str]
    active_project_id: UUID | None


def _parse_permissions(header_value: str | None) -> frozenset[str]:
    if not header_value:
        return frozenset()
    return frozenset(p.strip() for p in header_value.split(",") if p.strip())


async def require_current_user(request: web.Request) -> UserContext:
    """Auth hook: reads RBAC v2 headers provided by auth-proxy."""
    user_header = request.headers.get(USER_ID_HEADER)
    if user_header is None:
        raise web.HTTPUnauthorized(
            reason=f"Header {USER_ID_HEADER} is required"
        )
    try:
        user_id = UUID(user_header)
    except ValueError as exc:
        raise web.HTTPBadRequest(text=f"Invalid {USER_ID_HEADER}") from exc

    project_header = request.headers.get(PROJECT_ID_HEADER)
    project_id: UUID | None = None
    if project_header:
        try:
            project_id = UUID(project_header)
        except ValueError as exc:
            raise web.HTTPBadRequest(text=f"Invalid {PROJECT_ID_HEADER}") from exc

    is_superadmin_raw = request.headers.get(IS_SUPERADMIN_HEADER, "false")
    is_superadmin = is_superadmin_raw.strip().lower() == "true"

    system_permissions = _parse_permissions(request.headers.get(SYSTEM_PERMISSIONS_HEADER))
    project_permissions = _parse_permissions(request.headers.get(PROJECT_PERMISSIONS_HEADER))

    return UserContext(
        user_id=user_id,
        is_superadmin=is_superadmin,
        system_permissions=system_permissions,
        project_permissions=project_permissions,
        active_project_id=project_id,
    )


def ensure_permission(user: UserContext, permission: str) -> None:
    """Raises HTTPForbidden if user lacks the given permission."""
    if user.is_superadmin:
        return
    if permission in user.project_permissions:
        return
    if permission in user.system_permissions:
        return
    raise web.HTTPForbidden(reason=f"Missing permission: {permission}")


def infer_project_role(user: UserContext) -> str:
    """Infer project role name from permissions for audit (owner/editor/viewer)."""
    if user.is_superadmin:
        return "owner"
    perms = user.project_permissions | user.system_permissions
    if "project.roles.manage" in perms:
        return "owner"
    if "runs.create" in perms or "experiments.create" in perms:
        return "editor"
    return "viewer"


def ensure_project_context(user: UserContext) -> UUID:
    """Returns active_project_id or raises HTTPBadRequest."""
    if user.active_project_id is None:
        raise web.HTTPBadRequest(reason="X-Project-Id header required")
    return user.active_project_id


def resolve_project_id(
    user: UserContext,
    project_id_str: str | None,
    *,
    require_role: tuple[str, ...] | None = None,
) -> UUID:
    """Resolve project_id from string or active_project_id. require_role is ignored (deprecated)."""
    if project_id_str is None:
        if user.active_project_id is None:
            raise web.HTTPBadRequest(
                text="project_id is required. Provide it in query parameter, request body, or X-Project-Id header"
            )
        return user.active_project_id
    try:
        return UUID(project_id_str)
    except ValueError as exc:
        raise web.HTTPBadRequest(text="Invalid project_id") from exc


def ensure_project_access(
    user: UserContext,
    project_id: UUID,
    *,
    require_role: tuple[str, ...] | None = None,
) -> None:
    """Deprecated: kept for compatibility. Use ensure_permission() instead."""
    # In the new RBAC model project access is validated via project_permissions.
    # If user has any project permissions or is superadmin, they have basic access.
    if user.is_superadmin:
        return
    if user.project_permissions or user.system_permissions:
        return
    raise web.HTTPForbidden(reason="User does not belong to project")


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
        run_repository = RunRepository(pool)
        return ExperimentService(repository, run_repository)

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


async def get_artifact_service(request: web.Request) -> ArtifactService:
    async def builder(_: web.Request) -> ArtifactService:
        pool = await get_pool()
        artifact_repo = ArtifactRepository(pool)
        run_repo = RunRepository(pool)
        return ArtifactService(artifact_repo, run_repo)

    return await _get_or_create_service(request, _ARTIFACT_SERVICE_KEY, builder)


async def get_backfill_service(request: web.Request) -> BackfillService:
    async def builder(_: web.Request) -> BackfillService:
        pool = await get_pool()
        backfill_repo = BackfillTaskRepository(pool)
        profile_repo = ConversionProfileRepository(pool)
        sensor_repo = SensorRepository(pool)
        return BackfillService(backfill_repo, profile_repo, sensor_repo)

    return await _get_or_create_service(request, _BACKFILL_SERVICE_KEY, builder)
