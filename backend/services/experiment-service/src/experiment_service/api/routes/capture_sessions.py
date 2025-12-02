"""Capture session endpoints."""
from __future__ import annotations

from datetime import datetime, timezone

from aiohttp import web
from pydantic import ValidationError

from experiment_service.api.utils import paginated_response, pagination_params, parse_uuid, read_json
from experiment_service.core.exceptions import NotFoundError
from experiment_service.domain.dto import CaptureSessionCreateDTO, CaptureSessionUpdateDTO
from experiment_service.domain.enums import CaptureSessionStatus
from experiment_service.domain.models import CaptureSession
from experiment_service.services.dependencies import (
    ensure_project_access,
    get_capture_session_service,
    get_run_service,
    require_current_user,
    resolve_project_id,
)

routes = web.RouteTableDef()


def _session_response(session: CaptureSession) -> dict:
    return session.model_dump(mode="json")


async def _ensure_run(request: web.Request, project_id, run_id):
    run_service = await get_run_service(request)
    try:
        await run_service.get_run(project_id, run_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc


@routes.get("/api/v1/runs/{run_id}/capture-sessions")
async def list_capture_sessions(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_project_access(user, project_id)
    run_id = parse_uuid(request.match_info["run_id"], "run_id")
    await _ensure_run(request, project_id, run_id)
    service = await get_capture_session_service(request)
    limit, offset = pagination_params(request)
    sessions, total = await service.list_sessions_for_run(
        project_id, run_id, limit=limit, offset=offset
    )
    payload = paginated_response(
        [_session_response(item) for item in sessions],
        limit=limit,
        offset=offset,
        key="capture_sessions",
        total=total,
    )
    return web.json_response(payload)


@routes.post("/api/v1/runs/{run_id}/capture-sessions")
async def create_capture_session(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_project_access(user, project_id, require_role=("owner", "editor"))
    run_id = parse_uuid(request.match_info["run_id"], "run_id")
    await _ensure_run(request, project_id, run_id)
    body = await read_json(request)
    body["project_id"] = project_id
    body["run_id"] = run_id
    try:
        dto = CaptureSessionCreateDTO.model_validate(body)
    except ValidationError as exc:
        raise web.HTTPBadRequest(text=exc.json()) from exc
    service = await get_capture_session_service(request)
    session = await service.create_session(dto)
    return web.json_response(_session_response(session), status=201)


@routes.post("/api/v1/runs/{run_id}/capture-sessions/{session_id}/stop")
async def stop_capture_session(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_project_access(user, project_id, require_role=("owner", "editor"))
    run_id = parse_uuid(request.match_info["run_id"], "run_id")
    session_id = parse_uuid(request.match_info["session_id"], "session_id")
    await _ensure_run(request, project_id, run_id)
    body = await read_json(request)
    status_value = body.get("status", CaptureSessionStatus.SUCCEEDED.value)
    try:
        status = CaptureSessionStatus(status_value)
    except ValueError as exc:
        raise web.HTTPBadRequest(text="Invalid status value") from exc
    stopped_at = body.get("stopped_at") or datetime.now(timezone.utc)
    dto = CaptureSessionUpdateDTO(
        status=status,
        stopped_at=stopped_at,
        archived=body.get("archived"),
        notes=body.get("notes"),
    )
    service = await get_capture_session_service(request)
    try:
        session = await service.update_session(project_id, session_id, dto)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.json_response(_session_response(session))


@routes.delete("/api/v1/runs/{run_id}/capture-sessions/{session_id}")
async def delete_capture_session(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_project_access(user, project_id, require_role=("owner", "editor"))
    run_id = parse_uuid(request.match_info["run_id"], "run_id")
    session_id = parse_uuid(request.match_info["session_id"], "session_id")
    await _ensure_run(request, project_id, run_id)
    service = await get_capture_session_service(request)
    try:
        await service.delete_session(project_id, session_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.Response(status=204)
