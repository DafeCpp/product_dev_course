"""Execution endpoints."""
from __future__ import annotations

from uuid import UUID

import structlog
from aiohttp import web

from backend_common.aiohttp_app import read_json
from script_service.dependencies import (
    ensure_permission,
    extract_user,
    get_execution_dispatcher,
)
from script_service.domain.models import ExecutionStatus
from script_service.services.execution_dispatcher import (
    ExecutionNotFoundError,
    ScriptNotFoundError,
)

logger = structlog.get_logger(__name__)

routes = web.RouteTableDef()


def _parse_uuid(value: str, label: str) -> UUID:
    try:
        return UUID(value)
    except ValueError as exc:
        raise web.HTTPBadRequest(text=f"Invalid {label}") from exc


def _can_view_executions(user) -> bool:  # type: ignore[no-untyped-def]
    return (
        user.is_superadmin
        or "scripts.view_logs" in user.permissions
        or "scripts.view_logs" in user.system_permissions
        or "scripts.execute" in user.permissions
        or "scripts.execute" in user.system_permissions
    )


@routes.post("/api/v1/scripts/{id}/execute")
async def execute_script(request: web.Request) -> web.Response:
    user = extract_user(request)
    ensure_permission(user, "scripts.execute")

    script_id = _parse_uuid(request.match_info["id"], "id")
    body = await read_json(request)
    parameters = body.get("parameters", {})
    target_instance = body.get("target_instance")

    dispatcher = await get_execution_dispatcher(request)
    try:
        execution = await dispatcher.execute(
            user_id=user.user_id,
            script_id=script_id,
            parameters=parameters,
            target_instance=target_instance,
        )
    except ScriptNotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.json_response(execution.to_dict(), status=202)


@routes.post("/api/v1/executions/{id}/cancel")
async def cancel_execution(request: web.Request) -> web.Response:
    user = extract_user(request)
    ensure_permission(user, "scripts.execute")

    execution_id = _parse_uuid(request.match_info["id"], "id")
    dispatcher = await get_execution_dispatcher(request)
    try:
        execution = await dispatcher.cancel(user_id=user.user_id, execution_id=execution_id)
    except ExecutionNotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.json_response(execution.to_dict())


@routes.get("/api/v1/executions")
async def list_executions(request: web.Request) -> web.Response:
    user = extract_user(request)
    if not _can_view_executions(user):
        raise web.HTTPForbidden(
            reason="Missing permission: scripts.view_logs or scripts.execute"
        )

    query = request.rel_url.query
    script_id_raw = query.get("script_id")
    script_id: UUID | None = None
    if script_id_raw:
        script_id = _parse_uuid(script_id_raw, "script_id")

    status_raw = query.get("status")
    status: ExecutionStatus | None = None
    if status_raw:
        try:
            status = ExecutionStatus(status_raw)
        except ValueError as exc:
            raise web.HTTPBadRequest(text=f"Invalid status: {status_raw}") from exc

    requested_by_raw = query.get("requested_by")
    requested_by: UUID | None = None
    if requested_by_raw:
        requested_by = _parse_uuid(requested_by_raw, "requested_by")

    try:
        limit = int(query.get("limit", "50"))
        offset = int(query.get("offset", "0"))
    except ValueError as exc:
        raise web.HTTPBadRequest(text="limit and offset must be integers") from exc

    dispatcher = await get_execution_dispatcher(request)
    executions = await dispatcher.list_executions(
        script_id=script_id,
        status=status,
        requested_by=requested_by,
        limit=limit,
        offset=offset,
    )
    return web.json_response({"executions": [e.to_dict() for e in executions]})


@routes.get("/api/v1/executions/{id}")
async def get_execution(request: web.Request) -> web.Response:
    user = extract_user(request)
    if not _can_view_executions(user):
        raise web.HTTPForbidden(
            reason="Missing permission: scripts.view_logs or scripts.execute"
        )

    execution_id = _parse_uuid(request.match_info["id"], "id")
    dispatcher = await get_execution_dispatcher(request)
    try:
        execution = await dispatcher.get_execution(execution_id)
    except ExecutionNotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.json_response(execution.to_dict())
