"""Sensor management endpoints."""
from __future__ import annotations

from uuid import UUID

from aiohttp import web
from pydantic import ValidationError

from experiment_service.api.utils import (
    paginated_response,
    pagination_params,
    parse_datetime,
    parse_uuid,
    read_json,
)
from experiment_service.core.exceptions import (
    IdempotencyConflictError,
    InvalidStatusTransitionError,
    NotFoundError,
)
from experiment_service.domain.dto import (
    ConversionProfileInputDTO,
    SensorCreateDTO,
    SensorUpdateDTO,
)
from experiment_service.domain.enums import SensorStatus
from experiment_service.domain.models import Sensor
from experiment_service.services.dependencies import (
    ensure_project_access,
    get_idempotency_service,
    get_sensor_service,
    require_current_user,
    resolve_project_id,
)
from experiment_service.services.idempotency import IDEMPOTENCY_HEADER, IdempotencyService

routes = web.RouteTableDef()


def _sensor_response(sensor: Sensor) -> dict:
    return sensor.model_dump(mode="json")


@routes.post("/api/v1/sensors")
async def register_sensor(request: web.Request):
    user = await require_current_user(request)
    idempotency_service = await get_idempotency_service(request)
    body = await read_json(request)
    project_id = resolve_project_id(
        user, body.get("project_id"), require_role=("owner", "editor")
    )
    body["project_id"] = project_id
    conversion_payload = body.pop("conversion_profile", None)
    try:
        dto = SensorCreateDTO.model_validate(body)
    except ValidationError as exc:
        raise web.HTTPBadRequest(text=exc.json()) from exc
    profile_dto = None
    if conversion_payload is not None:
        try:
            profile_dto = ConversionProfileInputDTO.model_validate(conversion_payload)
        except ValidationError as exc:
            raise web.HTTPBadRequest(text=exc.json()) from exc
    body_for_hash = dto.model_dump(mode="json")
    idempotency_key = request.headers.get(IDEMPOTENCY_HEADER)
    serialized_body, body_hash = IdempotencyService.canonical_body(body_for_hash)
    if idempotency_key:
        try:
            cached = await idempotency_service.get_cached_response(
                idempotency_key, user.user_id, request.rel_url.path, body_hash
            )
        except IdempotencyConflictError as exc:
            raise web.HTTPConflict(text=str(exc)) from exc
        if cached:
            return IdempotencyService.build_response(cached)
    service = await get_sensor_service(request)
    try:
        sensor, token = await service.register_sensor(
            dto, created_by=user.user_id, initial_profile=profile_dto
        )
    except InvalidStatusTransitionError as exc:
        raise web.HTTPBadRequest(text=str(exc)) from exc
    payload = {"sensor": _sensor_response(sensor), "token": token}
    if idempotency_key:
        await idempotency_service.store_response(
            idempotency_key,
            user.user_id,
            request.rel_url.path,
            body_hash,
            201,
            payload,
        )
    return web.json_response(payload, status=201)


def _parse_project_ids_header(header_value: str | None) -> list[UUID]:
    """Parse X-Project-Ids header (comma-separated UUIDs). Returns list of UUIDs, empty if invalid."""
    if not header_value or not header_value.strip():
        return []
    result = []
    for part in header_value.split(","):
        part = part.strip()
        if not part:
            continue
        try:
            result.append(UUID(part))
        except (ValueError, TypeError):
            continue
    return result


@routes.get("/api/v1/sensors")
async def list_sensors(request: web.Request):
    user = await require_current_user(request)
    service = await get_sensor_service(request)
    limit, offset = pagination_params(request)

    # Optional filters
    status_raw = request.rel_url.query.get("status")
    sensor_status = SensorStatus(status_raw) if status_raw else None
    created_after = parse_datetime(request.rel_url.query.get("created_after"), "created_after")
    created_before = parse_datetime(request.rel_url.query.get("created_before"), "created_before")

    # project_id is optional - if not provided, return sensors from all accessible projects
    project_id_query = request.rel_url.query.get("project_id")
    if project_id_query:
        project_id = resolve_project_id(user, project_id_query)
        sensors, total = await service.list_sensors(
            project_id, limit=limit, offset=offset,
            status=sensor_status,
            created_after=created_after,
            created_before=created_before,
        )
    else:
        # When auth-proxy sets X-Project-Ids (list of user's project IDs), use them
        project_ids_header = request.headers.get("X-Project-Ids")
        project_ids = _parse_project_ids_header(project_ids_header)
        if project_ids:
            sensors, total = await service.list_sensors_by_projects(
                project_ids, limit=limit, offset=offset
            )
        elif user.active_project_id:
            ensure_project_access(user, user.active_project_id)
            sensors, total = await service.list_sensors(
                user.active_project_id, limit=limit, offset=offset,
                status=sensor_status,
                created_after=created_after,
                created_before=created_before,
            )
        else:
            sensors, total = [], 0

    payload = paginated_response(
        [_sensor_response(sensor) for sensor in sensors],
        limit=limit,
        offset=offset,
        key="sensors",
        total=total,
    )
    return web.json_response(payload)


@routes.get("/api/v1/sensors/{sensor_id}")
async def get_sensor(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    sensor_id = parse_uuid(request.match_info["sensor_id"], "sensor_id")
    service = await get_sensor_service(request)
    try:
        sensor = await service.get_sensor(project_id, sensor_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    ensure_project_access(user, sensor.project_id)
    return web.json_response(_sensor_response(sensor))


@routes.patch("/api/v1/sensors/{sensor_id}")
async def update_sensor(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(
        user, request.rel_url.query.get("project_id"), require_role=("owner", "editor")
    )
    sensor_id = parse_uuid(request.match_info["sensor_id"], "sensor_id")
    body = await read_json(request)
    try:
        dto = SensorUpdateDTO.model_validate(body)
    except ValidationError as exc:
        raise web.HTTPBadRequest(text=exc.json()) from exc
    service = await get_sensor_service(request)
    try:
        sensor = await service.update_sensor(project_id, sensor_id, dto)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.json_response(_sensor_response(sensor))


@routes.delete("/api/v1/sensors/{sensor_id}")
async def delete_sensor(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(
        user, request.rel_url.query.get("project_id"), require_role=("owner", "editor")
    )
    sensor_id = parse_uuid(request.match_info["sensor_id"], "sensor_id")
    service = await get_sensor_service(request)
    try:
        await service.delete_sensor(project_id, sensor_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    except InvalidStatusTransitionError as exc:
        raise web.HTTPBadRequest(text=str(exc)) from exc
    return web.Response(status=204)


@routes.post("/api/v1/sensors/{sensor_id}/rotate-token")
async def rotate_sensor_token(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(
        user, request.rel_url.query.get("project_id"), require_role=("owner", "editor")
    )
    sensor_id = parse_uuid(request.match_info["sensor_id"], "sensor_id")
    service = await get_sensor_service(request)
    try:
        sensor, token = await service.rotate_token(project_id, sensor_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.json_response({"sensor": _sensor_response(sensor), "token": token})


@routes.post("/api/v1/sensors/{sensor_id}/projects")
async def add_sensor_project(request: web.Request):
    """Add a sensor to a project."""
    user = await require_current_user(request)
    sensor_id = parse_uuid(request.match_info["sensor_id"], "sensor_id")
    body = await read_json(request)

    if "project_id" not in body:
        raise web.HTTPBadRequest(text="project_id is required")

    # Verify user has owner/editor role in the target project
    project_id = resolve_project_id(
        user, body["project_id"], require_role=("owner", "editor")
    )

    # Verify sensor exists
    # We don't require access to existing projects - if user has owner/editor role
    # in the target project, they can add the sensor to it
    service = await get_sensor_service(request)
    try:
        # Just verify sensor exists by trying to get it
        # We use get_by_id which doesn't require project_id
        from experiment_service.repositories.sensors import SensorRepository
        from backend_common.db.pool import get_pool_service as get_pool
        pool = await get_pool()
        repo = SensorRepository(pool)
        await repo.get_by_id(sensor_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc

    try:
        await service.add_sensor_project(sensor_id, project_id)
    except Exception as exc:
        raise web.HTTPBadRequest(text=str(exc)) from exc

    return web.Response(status=204)


@routes.delete("/api/v1/sensors/{sensor_id}/projects/{project_id}")
async def remove_sensor_project(request: web.Request):
    """Remove a sensor from a project."""
    user = await require_current_user(request)
    sensor_id = parse_uuid(request.match_info["sensor_id"], "sensor_id")
    project_id = parse_uuid(request.match_info["project_id"], "project_id")

    # Verify user has owner/editor role in the project
    resolve_project_id(user, str(project_id), require_role=("owner", "editor"))
    ensure_project_access(user, project_id, require_role=("owner", "editor"))

    service = await get_sensor_service(request)
    try:
        await service.remove_sensor_project(sensor_id, project_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc

    return web.Response(status=204)


@routes.get("/api/v1/sensors/{sensor_id}/projects")
async def get_sensor_projects(request: web.Request):
    """Get all projects associated with a sensor."""
    user = await require_current_user(request)
    sensor_id = parse_uuid(request.match_info["sensor_id"], "sensor_id")

    service = await get_sensor_service(request)
    try:
        project_ids = await service.get_sensor_projects(sensor_id)
        if not project_ids:
            raise web.HTTPNotFound(text="Sensor not found")
        # Filter to only projects user has access to
        accessible_projects = []
        for pid in project_ids:
            # Check if project is in user's project_roles
            if pid in user.project_roles:
                accessible_projects.append(str(pid))
        # If user has no access to any project, return 403
        if not accessible_projects:
            raise web.HTTPForbidden(text="Access denied to sensor")
        return web.json_response({"project_ids": accessible_projects})
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
