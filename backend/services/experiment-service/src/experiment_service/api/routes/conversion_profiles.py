"""Conversion profile endpoints."""
from __future__ import annotations

from aiohttp import web
from pydantic import ValidationError

from experiment_service.api.utils import paginated_response, pagination_params, parse_uuid, read_json
from experiment_service.core.exceptions import InvalidStatusTransitionError, NotFoundError
from experiment_service.domain.dto import ConversionProfileInputDTO
from experiment_service.domain.models import ConversionProfile
from experiment_service.services.dependencies import (
    ensure_project_access,
    get_conversion_profile_service,
    require_current_user,
    resolve_project_id,
)

routes = web.RouteTableDef()


def _profile_response(profile: ConversionProfile) -> dict:
    return profile.model_dump(mode="json")


@routes.post("/api/v1/sensors/{sensor_id}/conversion-profiles")
async def create_profile(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(
        user, request.rel_url.query.get("project_id"), require_role=("owner", "editor")
    )
    sensor_id = parse_uuid(request.match_info["sensor_id"], "sensor_id")
    payload = await read_json(request)
    try:
        dto = ConversionProfileInputDTO.model_validate(payload)
    except ValidationError as exc:
        raise web.HTTPBadRequest(text=exc.json()) from exc
    service = await get_conversion_profile_service(request)
    try:
        profile = await service.create_profile(
            project_id,
            sensor_id,
            dto,
            created_by=user.user_id,
        )
    except (InvalidStatusTransitionError, NotFoundError) as exc:
        status = web.HTTPBadRequest if isinstance(exc, InvalidStatusTransitionError) else web.HTTPNotFound
        raise status(text=str(exc)) from exc
    return web.json_response(_profile_response(profile), status=201)


@routes.get("/api/v1/sensors/{sensor_id}/conversion-profiles")
async def list_profiles(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    sensor_id = parse_uuid(request.match_info["sensor_id"], "sensor_id")
    service = await get_conversion_profile_service(request)
    limit, offset = pagination_params(request)
    profiles, total = await service.list_profiles(
        project_id, sensor_id, limit=limit, offset=offset
    )
    payload = paginated_response(
        [_profile_response(profile) for profile in profiles],
        limit=limit,
        offset=offset,
        key="conversion_profiles",
        total=total,
    )
    return web.json_response(payload)


@routes.post("/api/v1/sensors/{sensor_id}/conversion-profiles/{profile_id}/publish")
async def publish_profile(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(
        user, request.rel_url.query.get("project_id"), require_role=("owner",)
    )
    sensor_id = parse_uuid(request.match_info["sensor_id"], "sensor_id")
    profile_id = parse_uuid(request.match_info["profile_id"], "profile_id")
    service = await get_conversion_profile_service(request)
    try:
        profile = await service.publish_profile(
            project_id,
            sensor_id,
            profile_id,
            published_by=user.user_id,
        )
    except InvalidStatusTransitionError as exc:
        raise web.HTTPBadRequest(text=str(exc)) from exc
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.json_response(_profile_response(profile))
