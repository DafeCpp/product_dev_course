"""Script CRUD endpoints."""
from __future__ import annotations

from uuid import UUID

import structlog
from aiohttp import web

from backend_common.aiohttp_app import read_json
from script_service.dependencies import (
    ensure_permission,
    extract_user,
    get_script_manager,
)
from script_service.domain.models import ScriptType
from script_service.services.script_manager import ScriptNotFoundError

logger = structlog.get_logger(__name__)

routes = web.RouteTableDef()


def _parse_uuid(value: str, label: str) -> UUID:
    try:
        return UUID(value)
    except ValueError as exc:
        raise web.HTTPBadRequest(text=f"Invalid {label}") from exc


@routes.post("/api/v1/scripts")
async def create_script(request: web.Request) -> web.Response:
    user = extract_user(request)
    ensure_permission(user, "scripts.manage")

    body = await read_json(request)
    name = body.get("name")
    if not name or not isinstance(name, str):
        raise web.HTTPBadRequest(text="'name' is required")
    target_service = body.get("target_service")
    if not target_service or not isinstance(target_service, str):
        raise web.HTTPBadRequest(text="'target_service' is required")
    script_body = body.get("script_body")
    if not script_body or not isinstance(script_body, str):
        raise web.HTTPBadRequest(text="'script_body' is required")

    script_type_raw = body.get("script_type", "python")
    try:
        script_type = ScriptType(script_type_raw)
    except ValueError as exc:
        raise web.HTTPBadRequest(text=f"Invalid script_type: {script_type_raw}") from exc

    description = body.get("description")
    parameters_schema = body.get("parameters_schema", {})
    timeout_sec = int(body.get("timeout_sec", 30))

    manager = await get_script_manager(request)
    script = await manager.create_script(
        name=name,
        description=description,
        target_service=target_service,
        script_type=script_type,
        script_body=script_body,
        parameters_schema=parameters_schema,
        timeout_sec=timeout_sec,
        created_by=user.user_id,
    )
    return web.json_response(script.to_dict(), status=201)


@routes.get("/api/v1/scripts")
async def list_scripts(request: web.Request) -> web.Response:
    user = extract_user(request)
    if not (
        "scripts.manage" in user.permissions
        or "scripts.manage" in user.system_permissions
        or "scripts.execute" in user.permissions
        or "scripts.execute" in user.system_permissions
        or user.is_superadmin
    ):
        raise web.HTTPForbidden(reason="Missing permission: scripts.manage or scripts.execute")

    query = request.rel_url.query
    target_service = query.get("target_service") or None
    is_active_raw = query.get("is_active")
    is_active: bool | None = None
    if is_active_raw is not None:
        is_active = is_active_raw.lower() not in ("false", "0", "no")

    try:
        limit = int(query.get("limit", "50"))
        offset = int(query.get("offset", "0"))
    except ValueError as exc:
        raise web.HTTPBadRequest(text="limit and offset must be integers") from exc

    manager = await get_script_manager(request)
    scripts = await manager.list_scripts(
        target_service=target_service,
        is_active=is_active,
        limit=limit,
        offset=offset,
    )
    return web.json_response({"scripts": [s.to_dict() for s in scripts]})


@routes.get("/api/v1/scripts/{id}")
async def get_script(request: web.Request) -> web.Response:
    user = extract_user(request)
    if not (
        "scripts.manage" in user.permissions
        or "scripts.manage" in user.system_permissions
        or "scripts.execute" in user.permissions
        or "scripts.execute" in user.system_permissions
        or user.is_superadmin
    ):
        raise web.HTTPForbidden(reason="Missing permission: scripts.manage or scripts.execute")

    script_id = _parse_uuid(request.match_info["id"], "id")
    manager = await get_script_manager(request)
    try:
        script = await manager.get_script(script_id)
    except ScriptNotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.json_response(script.to_dict())


@routes.patch("/api/v1/scripts/{id}")
async def update_script(request: web.Request) -> web.Response:
    user = extract_user(request)
    ensure_permission(user, "scripts.manage")

    script_id = _parse_uuid(request.match_info["id"], "id")
    body = await read_json(request)

    allowed_fields = {
        "name", "description", "target_service", "script_body",
        "parameters_schema", "timeout_sec", "is_active",
    }
    fields = {k: v for k, v in body.items() if k in allowed_fields}

    if "script_type" in body:
        try:
            fields["script_type"] = ScriptType(body["script_type"])
        except ValueError as exc:
            raise web.HTTPBadRequest(text=f"Invalid script_type: {body['script_type']}") from exc

    manager = await get_script_manager(request)
    try:
        script = await manager.update_script(script_id, **fields)
    except ScriptNotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.json_response(script.to_dict())


@routes.delete("/api/v1/scripts/{id}")
async def delete_script(request: web.Request) -> web.Response:
    user = extract_user(request)
    ensure_permission(user, "scripts.manage")

    script_id = _parse_uuid(request.match_info["id"], "id")
    manager = await get_script_manager(request)
    try:
        await manager.delete_script(script_id)
    except ScriptNotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.Response(status=204)
