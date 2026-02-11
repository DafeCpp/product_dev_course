"""Capture session endpoints."""
from __future__ import annotations

from datetime import datetime, timezone

from aiohttp import web
from pydantic import ValidationError

from experiment_service.api.utils import paginated_response, pagination_params, parse_uuid, read_json
from experiment_service.core.exceptions import (
    IdempotencyConflictError,
    InvalidStatusTransitionError,
    NotFoundError,
)
from experiment_service.domain.dto import CaptureSessionCreateDTO, CaptureSessionUpdateDTO
from experiment_service.domain.enums import CaptureSessionStatus
from experiment_service.domain.models import CaptureSession
from experiment_service.services.dependencies import (
    ensure_project_access,
    get_capture_session_service,
    get_capture_session_event_service,
    get_idempotency_service,
    get_run_service,
    get_webhook_service,
    require_current_user,
    resolve_project_id,
)
from experiment_service.services.idempotency import IDEMPOTENCY_HEADER, IdempotencyService

routes = web.RouteTableDef()

EVENT_CREATED = "capture_session.created"
EVENT_STOPPED = "capture_session.stopped"
EVENT_BACKFILL_STARTED = "capture_session.backfill_started"
EVENT_BACKFILL_COMPLETED = "capture_session.backfill_completed"


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
    body.setdefault("initiated_by", user.user_id)
    # UX: "Старт отсчёта" должен реально стартовать запись.
    # По умолчанию создаём активную сессию (RUNNING) и фиксируем started_at.
    # Клиент может явно передать другой статус (например, draft) для технических сценариев.
    body.setdefault("status", CaptureSessionStatus.RUNNING.value)
    body.setdefault("started_at", datetime.now(timezone.utc))
    idempotency_service = await get_idempotency_service(request)
    idempotency_key = request.headers.get(IDEMPOTENCY_HEADER)
    try:
        dto = CaptureSessionCreateDTO.model_validate(body)
    except ValidationError as exc:
        raise web.HTTPBadRequest(text=exc.json()) from exc
    service = await get_capture_session_service(request)
    serialized_body, body_hash = IdempotencyService.canonical_body(body)
    if idempotency_key:
        try:
            cached = await idempotency_service.get_cached_response(
                idempotency_key, user.user_id, request.rel_url.path, body_hash
            )
        except IdempotencyConflictError as exc:
            raise web.HTTPConflict(text=str(exc)) from exc
        if cached:
            return IdempotencyService.build_response(cached)
    try:
        session = await service.create_session(dto)
    except InvalidStatusTransitionError as exc:
        raise web.HTTPBadRequest(text=str(exc)) from exc
    actor_role = user.project_roles.get(project_id)
    if actor_role:
        audit = await get_capture_session_event_service(request)
        await audit.record_event(
            capture_session_id=session.id,
            event_type=EVENT_CREATED,
            actor_id=user.user_id,
            actor_role=actor_role,
            payload={
                "run_id": str(run_id),
                "ordinal_number": session.ordinal_number,
                "status": session.status.value,
                "notes": session.notes,
            },
        )
        webhooks = await get_webhook_service(request)
        await webhooks.emit(
            project_id=project_id,
            event_type=EVENT_CREATED,
            payload={
                "run_id": str(run_id),
                "capture_session_id": str(session.id),
                "ordinal_number": session.ordinal_number,
                "status": session.status.value,
                "notes": session.notes,
            },
        )
    response_payload = _session_response(session)
    if idempotency_key:
        try:
            await idempotency_service.store_response(
                idempotency_key,
                user.user_id,
                request.rel_url.path,
                body_hash,
                201,
                response_payload,
            )
        except IdempotencyConflictError as exc:
            raise web.HTTPConflict(text=str(exc)) from exc
    return web.json_response(response_payload, status=201)


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
    actor_role = user.project_roles.get(project_id)
    if actor_role:
        audit = await get_capture_session_event_service(request)
        await audit.record_event(
            capture_session_id=session.id,
            event_type=EVENT_STOPPED,
            actor_id=user.user_id,
            actor_role=actor_role,
            payload={
                "run_id": str(run_id),
                "status": session.status.value,
                "stopped_at": session.stopped_at.isoformat() if session.stopped_at else None,
                "archived": session.archived,
                "notes": session.notes,
            },
        )
        webhooks = await get_webhook_service(request)
        await webhooks.emit(
            project_id=project_id,
            event_type=EVENT_STOPPED,
            payload={
                "run_id": str(run_id),
                "capture_session_id": str(session.id),
                "status": session.status.value,
                "stopped_at": session.stopped_at.isoformat() if session.stopped_at else None,
                "archived": session.archived,
                "notes": session.notes,
            },
        )
    return web.json_response(_session_response(session))


@routes.get("/api/v1/runs/{run_id}/capture-sessions/{session_id}/events")
async def list_capture_session_events(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_project_access(user, project_id)
    run_id = parse_uuid(request.match_info["run_id"], "run_id")
    session_id = parse_uuid(request.match_info["session_id"], "session_id")
    await _ensure_run(request, project_id, run_id)
    capture_service = await get_capture_session_service(request)
    try:
        session = await capture_service.get_session(project_id, session_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    if session.run_id != run_id:
        raise web.HTTPNotFound(text="Capture session not found")
    audit = await get_capture_session_event_service(request)
    limit, offset = pagination_params(request)
    events, total = await audit.list_events(session_id, limit=limit, offset=offset)
    payload = paginated_response(
        [evt.model_dump(mode="json") for evt in events],
        limit=limit,
        offset=offset,
        key="events",
        total=total,
    )
    return web.json_response(payload)


@routes.post("/api/v1/runs/{run_id}/capture-sessions/{session_id}/backfill/start")
async def start_backfill(request: web.Request):
    """Transition a succeeded capture session into backfilling mode.

    While in backfilling status the telemetry-ingest-service treats incoming
    data as normal (not late), so the sensor can re-send missing readings.
    """
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_project_access(user, project_id, require_role=("owner", "editor"))
    run_id = parse_uuid(request.match_info["run_id"], "run_id")
    session_id = parse_uuid(request.match_info["session_id"], "session_id")
    await _ensure_run(request, project_id, run_id)
    service = await get_capture_session_service(request)
    try:
        session = await service.start_backfill(project_id, session_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    except InvalidStatusTransitionError as exc:
        raise web.HTTPBadRequest(text=str(exc)) from exc

    actor_role = user.project_roles.get(project_id)
    if actor_role:
        audit = await get_capture_session_event_service(request)
        await audit.record_event(
            capture_session_id=session.id,
            event_type=EVENT_BACKFILL_STARTED,
            actor_id=user.user_id,
            actor_role=actor_role,
            payload={
                "run_id": str(run_id),
                "status": session.status.value,
            },
        )
        webhooks = await get_webhook_service(request)
        await webhooks.emit(
            project_id=project_id,
            event_type=EVENT_BACKFILL_STARTED,
            payload={
                "run_id": str(run_id),
                "capture_session_id": str(session.id),
                "status": session.status.value,
            },
        )
    return web.json_response(_session_response(session))


@routes.post("/api/v1/runs/{run_id}/capture-sessions/{session_id}/backfill/complete")
async def complete_backfill(request: web.Request):
    """Finish backfill: attach late telemetry records and return to succeeded.

    Response includes ``attached_records`` — the number of late telemetry rows
    that were retroactively linked to the capture session.
    """
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_project_access(user, project_id, require_role=("owner", "editor"))
    run_id = parse_uuid(request.match_info["run_id"], "run_id")
    session_id = parse_uuid(request.match_info["session_id"], "session_id")
    await _ensure_run(request, project_id, run_id)
    service = await get_capture_session_service(request)
    try:
        session, attached = await service.complete_backfill(project_id, session_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    except InvalidStatusTransitionError as exc:
        raise web.HTTPBadRequest(text=str(exc)) from exc

    actor_role = user.project_roles.get(project_id)
    if actor_role:
        audit = await get_capture_session_event_service(request)
        await audit.record_event(
            capture_session_id=session.id,
            event_type=EVENT_BACKFILL_COMPLETED,
            actor_id=user.user_id,
            actor_role=actor_role,
            payload={
                "run_id": str(run_id),
                "status": session.status.value,
                "attached_records": attached,
            },
        )
        webhooks = await get_webhook_service(request)
        await webhooks.emit(
            project_id=project_id,
            event_type=EVENT_BACKFILL_COMPLETED,
            payload={
                "run_id": str(run_id),
                "capture_session_id": str(session.id),
                "status": session.status.value,
                "attached_records": attached,
            },
        )
    resp = _session_response(session)
    resp["attached_records"] = attached
    return web.json_response(resp)


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
    except InvalidStatusTransitionError as exc:
        raise web.HTTPBadRequest(text=str(exc)) from exc
    return web.Response(status=204)
