"""Sensor management endpoints."""
from __future__ import annotations

from aiohttp import web
from pydantic import ValidationError

from experiment_service.api.utils import paginated_response, pagination_params, parse_uuid, read_json
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


@routes.get("/api/v1/sensors")
async def list_sensors(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    service = await get_sensor_service(request)
    limit, offset = pagination_params(request)
    sensors, total = await service.list_sensors(project_id, limit=limit, offset=offset)
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
