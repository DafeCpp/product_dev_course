"""Sensor management endpoints."""
from __future__ import annotations

from uuid import UUID

from aiohttp import web
from pydantic import ValidationError

from experiment_service.api.utils import (
    paginated_response,
    pagination_params,
    parse_datetime,
    parse_status_filter,
    parse_uuid,
    read_json,
)
from backend_common.core.exceptions import ConflictError
from experiment_service.core.exceptions import (
    IdempotencyConflictError,
    NotFoundError,
)
from experiment_service.domain.dto import (
    ConversionProfileInputDTO,
    SensorCreateDTO,
    SensorUpdateDTO,
)
from experiment_service.domain.enums import SensorProjectRequestStatus, SensorStatus
from experiment_service.domain.models import Sensor, SensorProjectRequest
from experiment_service.services.dependencies import (
    ensure_permission,
    ensure_project_context,
    get_idempotency_service,
    get_sensor_project_request_service,
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
    project_id = resolve_project_id(user, body.get("project_id"))
    ensure_permission(user, "experiments.create")
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
    reservation = None
    if idempotency_key:
        try:
            reservation, cached = await idempotency_service.reserve_or_get_cached(
                idempotency_key, user.user_id, request.rel_url.path, body_hash
            )
        except IdempotencyConflictError as exc:
            raise web.HTTPConflict(text="Conflict") from exc
        if cached is not None:
            return IdempotencyService.build_response(cached)
    service = await get_sensor_service(request)
    async with idempotency_service.guard_reservation(reservation):
        sensor, token = await service.register_sensor(
            dto, created_by=user.user_id, initial_profile=profile_dto
        )
    payload = {"sensor": _sensor_response(sensor), "token": token}
    if reservation is not None:
        await idempotency_service.complete_response(reservation, 201, payload)
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
    sensor_status = parse_status_filter(status_raw, SensorStatus)
    created_after = parse_datetime(request.rel_url.query.get("created_after"), "created_after")
    created_before = parse_datetime(request.rel_url.query.get("created_before"), "created_before")

    # project_id is optional - if not provided, return sensors from all accessible projects
    project_id_query = request.rel_url.query.get("project_id")
    if project_id_query:
        project_id = resolve_project_id(user, project_id_query)
        ensure_permission(user, "experiments.view")
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
            ensure_permission(user, "experiments.view")
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
    ensure_permission(user, "experiments.view")
    sensor_id = parse_uuid(request.match_info["sensor_id"], "sensor_id")
    service = await get_sensor_service(request)
    try:
        sensor = await service.get_sensor(project_id, sensor_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text="Resource not found") from exc
    return web.json_response(_sensor_response(sensor))


@routes.patch("/api/v1/sensors/{sensor_id}")
async def update_sensor(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_permission(user, "experiments.update")
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
        raise web.HTTPNotFound(text="Resource not found") from exc
    return web.json_response(_sensor_response(sensor))


@routes.delete("/api/v1/sensors/{sensor_id}")
async def delete_sensor(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_permission(user, "experiments.delete")
    sensor_id = parse_uuid(request.match_info["sensor_id"], "sensor_id")
    service = await get_sensor_service(request)
    try:
        await service.delete_sensor(project_id, sensor_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text="Resource not found") from exc
    return web.Response(status=204)


@routes.post("/api/v1/sensors/{sensor_id}/rotate-token")
async def rotate_sensor_token(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_permission(user, "experiments.update")
    sensor_id = parse_uuid(request.match_info["sensor_id"], "sensor_id")
    service = await get_sensor_service(request)
    try:
        sensor, token = await service.rotate_token(project_id, sensor_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text="Resource not found") from exc
    return web.json_response({"sensor": _sensor_response(sensor), "token": token})


def _request_response(item: SensorProjectRequest) -> dict:
    return item.model_dump(mode="json")


@routes.post("/api/v1/sensors/{sensor_id}/projects")
async def add_sensor_project(request: web.Request):
    """Request to attach a sensor to the caller's project.

    The sensor is attached only after a member of a project it already belongs
    to approves the request, so knowing a sensor UUID is not enough to read
    its telemetry.
    """
    user = await require_current_user(request)
    sensor_id = parse_uuid(request.match_info["sensor_id"], "sensor_id")
    body = await read_json(request)

    if "project_id" not in body:
        raise web.HTTPBadRequest(text="project_id is required")

    project_id = resolve_project_id(user, body["project_id"])
    ensure_permission(user, "experiments.create")

    service = await get_sensor_project_request_service(request)
    try:
        item = await service.request_attach(sensor_id, project_id, user.user_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text="Resource not found") from exc
    except ConflictError as exc:
        raise web.HTTPConflict(text=str(exc)) from exc
    return web.json_response(_request_response(item), status=202)


@routes.delete("/api/v1/sensors/{sensor_id}/projects/{project_id}")
async def remove_sensor_project(request: web.Request):
    """Remove a sensor from a project.

    Allowed from that project itself or from the sensor's primary project.
    """
    user = await require_current_user(request)
    sensor_id = parse_uuid(request.match_info["sensor_id"], "sensor_id")
    project_id = parse_uuid(request.match_info["project_id"], "project_id")
    caller_project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))

    ensure_permission(user, "experiments.delete")

    service = await get_sensor_service(request)
    if caller_project_id != project_id and not user.is_superadmin:
        try:
            sensor = await service.get_sensor(caller_project_id, sensor_id)
        except NotFoundError as exc:
            raise web.HTTPNotFound(text="Resource not found") from exc
        if sensor.project_id != caller_project_id:
            raise web.HTTPForbidden(text="Only the sensor's primary project can detach it from other projects")
    try:
        await service.remove_sensor_project(sensor_id, project_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text="Resource not found") from exc

    return web.Response(status=204)


@routes.get("/api/v1/sensor-project-requests")
async def list_sensor_project_requests(request: web.Request):
    """Incoming requests (for sensors of this project) or outgoing ones (made by it)."""
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_permission(user, "experiments.view")
    direction = request.rel_url.query.get("direction", "incoming")
    if direction not in ("incoming", "outgoing"):
        raise web.HTTPBadRequest(text="direction must be 'incoming' or 'outgoing'")
    status_raw = request.rel_url.query.get("status")
    try:
        status = SensorProjectRequestStatus(status_raw) if status_raw else None
    except ValueError as exc:
        raise web.HTTPBadRequest(text="Invalid status") from exc

    service = await get_sensor_project_request_service(request)
    items = await service.list_requests(project_id, direction, status)  # type: ignore[arg-type]
    return web.json_response({"requests": [_request_response(i) for i in items]})


async def _decide_request(request: web.Request, status: SensorProjectRequestStatus) -> web.Response:
    user = await require_current_user(request)
    request_id = parse_uuid(request.match_info["request_id"], "request_id")
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    service = await get_sensor_project_request_service(request)
    try:
        if status is SensorProjectRequestStatus.CANCELLED:
            ensure_permission(user, "experiments.create")
            item = await service.cancel(request_id, project_id, user.user_id)
        else:
            ensure_permission(user, "experiments.update")
            item = await service.decide(request_id, project_id, user.user_id, status)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text="Resource not found") from exc
    except ConflictError as exc:
        raise web.HTTPConflict(text=str(exc)) from exc
    return web.json_response(_request_response(item))


@routes.post("/api/v1/sensor-project-requests/{request_id}/approve")
async def approve_sensor_project_request(request: web.Request):
    """Approve from a project the sensor belongs to; attaches the sensor."""
    return await _decide_request(request, SensorProjectRequestStatus.APPROVED)


@routes.post("/api/v1/sensor-project-requests/{request_id}/reject")
async def reject_sensor_project_request(request: web.Request):
    """Reject from a project the sensor belongs to."""
    return await _decide_request(request, SensorProjectRequestStatus.REJECTED)


@routes.post("/api/v1/sensor-project-requests/{request_id}/cancel")
async def cancel_sensor_project_request(request: web.Request):
    """Withdraw a request from the project that made it."""
    return await _decide_request(request, SensorProjectRequestStatus.CANCELLED)


@routes.get("/api/v1/sensors/status-summary")
async def get_sensor_status_summary(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_permission(user, "experiments.view")
    service = await get_sensor_service(request)
    summary = await service.get_status_summary(project_id)
    return web.json_response(summary)


@routes.get("/api/v1/sensors/{sensor_id}/heartbeat-history")
async def get_sensor_heartbeat_history(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_permission(user, "experiments.view")
    sensor_id = parse_uuid(request.match_info["sensor_id"], "sensor_id")
    minutes_raw = request.rel_url.query.get("minutes", "60")
    try:
        minutes = int(minutes_raw)
        if minutes < 1 or minutes > 1440:
            raise ValueError("out of range")
    except ValueError:
        raise web.HTTPBadRequest(text="minutes must be an integer between 1 and 1440")
    service = await get_sensor_service(request)
    try:
        await service.get_sensor(project_id, sensor_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text="Resource not found") from exc
    timestamps = await service.get_heartbeat_history(sensor_id, minutes)
    return web.json_response(
        {
            "sensor_id": str(sensor_id),
            "timestamps": [ts.isoformat() for ts in timestamps],
            "count": len(timestamps),
        }
    )


@routes.get("/api/v1/sensors/{sensor_id}/projects")
async def get_sensor_projects(request: web.Request):
    """Get all projects associated with a sensor.

    Scoped: the caller must have access to a project the sensor belongs to.
    We verify the sensor is attached to the resolved ``project_id`` before
    disclosing the full list, otherwise a user could enumerate the project
    membership of arbitrary sensors.
    """
    user = await require_current_user(request)
    project_id = ensure_project_context(user)
    ensure_permission(user, "experiments.view")
    sensor_id = parse_uuid(request.match_info["sensor_id"], "sensor_id")

    service = await get_sensor_service(request)
    try:
        # 404 if the sensor is not attached to the caller's project.
        await service.get_sensor(project_id, sensor_id)
        project_ids = await service.get_sensor_projects(sensor_id)
        if not project_ids:
            raise web.HTTPNotFound(text="Sensor not found")
        return web.json_response({"project_ids": [str(pid) for pid in project_ids]})
    except NotFoundError as exc:
        raise web.HTTPNotFound(text="Resource not found") from exc
