"""Schema management endpoints."""
from __future__ import annotations

from aiohttp import web

from config_service.core.exceptions import (
    SchemaBreakingChangeError,
    SchemaSanityFailedError,
)
from config_service.domain.dto import SchemaUpdateRequest
from config_service.domain.enums import ConfigType
from config_service.domain.models import ConfigSchema
from config_service.services.dependencies import (
    get_schema_service,
    get_validation_service,
    require_current_user,
    ensure_permission,
)

routes = web.RouteTableDef()


def _schema_to_dict(s: ConfigSchema) -> dict[str, object]:
    return {
        "id": str(s.id),
        "config_type": s.config_type.value,
        "schema": s.schema,
        "version": s.version,
        "is_active": s.is_active,
        "created_by": s.created_by,
        "created_at": s.created_at.isoformat(),
    }


@routes.get("/api/v1/schemas")
async def list_schemas(request: web.Request) -> web.Response:
    require_current_user(request)
    svc = await get_schema_service(request)
    schemas = await svc.list_active()
    return web.json_response({"items": [_schema_to_dict(s) for s in schemas]})


@routes.get("/api/v1/schemas/{config_type}")
async def get_schema(request: web.Request) -> web.Response:
    require_current_user(request)
    try:
        config_type = ConfigType(request.match_info["config_type"])
    except ValueError:
        raise web.HTTPBadRequest(reason="Invalid config_type")

    svc = await get_schema_service(request)
    schema = await svc.get_active(config_type)
    if schema is None:
        raise web.HTTPNotFound()

    return web.json_response(_schema_to_dict(schema))


@routes.get("/api/v1/schemas/{config_type}/history")
async def get_schema_history(request: web.Request) -> web.Response:
    require_current_user(request)
    try:
        config_type = ConfigType(request.match_info["config_type"])
    except ValueError:
        raise web.HTTPBadRequest(reason="Invalid config_type")

    svc = await get_schema_service(request)
    history = await svc.list_history(config_type)
    return web.json_response({"items": [_schema_to_dict(s) for s in history]})


@routes.put("/api/v1/schemas/{config_type}")
async def update_schema(request: web.Request) -> web.Response:
    user = require_current_user(request)
    ensure_permission(user, "configs.schemas.manage")

    try:
        config_type = ConfigType(request.match_info["config_type"])
    except ValueError:
        raise web.HTTPBadRequest(reason="Invalid config_type")

    body = await request.json()
    try:
        dto = SchemaUpdateRequest.model_validate(body)
    except Exception as exc:
        raise web.HTTPUnprocessableEntity(reason=str(exc).replace("\n", " ").replace("\r", ""))

    svc = await get_schema_service(request)
    validation_svc = await get_validation_service(request)

    try:
        schema = await svc.update(config_type, dto.schema_, created_by=user.user_id)
    except SchemaBreakingChangeError as exc:
        raise web.HTTPUnprocessableEntity(
            text=str({"error": "Breaking schema changes", "violations": exc.violations}),
            content_type="application/json",
        )
    except SchemaSanityFailedError as exc:
        raise web.HTTPInternalServerError(
            text=str({"error": "Schema sanity check failed", "failures": exc.failures}),
            content_type="application/json",
        )

    validation_svc.invalidate_cache(config_type)

    return web.json_response(_schema_to_dict(schema), status=200)
