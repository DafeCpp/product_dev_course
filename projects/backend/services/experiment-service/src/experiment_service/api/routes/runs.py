"""Run endpoints."""
# pyright: reportMissingImports=false
from __future__ import annotations

from uuid import UUID

from aiohttp import web
from pydantic import ValidationError

from experiment_service.api.utils import (
    paginated_response,
    pagination_params,
    parse_datetime,
    parse_tags_filter,
    parse_uuid,
    read_json,
)
from experiment_service.core.exceptions import (
    IdempotencyConflictError,
    InvalidStatusTransitionError,
    NotFoundError,
    ScopeMismatchError,
)
from experiment_service.domain.dto import RunCreateDTO, RunUpdateDTO
from experiment_service.domain.enums import RunStatus
from experiment_service.domain.models import Run
from experiment_service.services.dependencies import (
    ensure_project_access,
    get_idempotency_service,
    get_experiment_service,
    get_run_service,
    get_run_event_service,
    get_webhook_service,
    require_current_user,
    resolve_project_id,
)
from experiment_service.services.idempotency import (
    IDEMPOTENCY_HEADER,
    IdempotencyService,
)

routes = web.RouteTableDef()

EVENT_RUN_STARTED = "run.started"
EVENT_RUN_FINISHED = "run.finished"
EVENT_RUN_ARCHIVED = "run.archived"
EVENT_RUN_STATUS_CHANGED = "run.status_changed"
EVENT_RUN_TAGS_UPDATED = "run.tags_updated"


def _run_response(run: Run) -> dict:
    return run.model_dump(mode="json")


async def _ensure_experiment(request: web.Request, project_id: UUID, experiment_id: UUID) -> None:
    experiment_service = await get_experiment_service(request)
    try:
        await experiment_service.get_experiment(project_id, experiment_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc


@routes.get("/api/v1/experiments/{experiment_id}/runs")
async def list_runs(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_project_access(user, project_id)
    experiment_id = parse_uuid(request.match_info["experiment_id"], "experiment_id")
    await _ensure_experiment(request, project_id, experiment_id)
    service = await get_run_service(request)
    limit, offset = pagination_params(request)

    # Optional filters
    status_raw = request.rel_url.query.get("status")
    status = RunStatus(status_raw) if status_raw else None
    tags = parse_tags_filter(request.rel_url.query.get("tags"))
    created_after = parse_datetime(request.rel_url.query.get("created_after"), "created_after")
    created_before = parse_datetime(request.rel_url.query.get("created_before"), "created_before")

    runs, total = await service.list_runs_for_experiment(
        project_id,
        experiment_id,
        limit=limit,
        offset=offset,
        status=status,
        tags=tags,
        created_after=created_after,
        created_before=created_before,
    )
    payload = paginated_response(
        [_run_response(item) for item in runs],
        limit=limit,
        offset=offset,
        key="runs",
        total=total,
    )
    return web.json_response(payload)


@routes.post("/api/v1/experiments/{experiment_id}/runs")
async def create_run(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(
        user, request.rel_url.query.get("project_id"), require_role=("owner", "editor")
    )
    experiment_id = parse_uuid(request.match_info["experiment_id"], "experiment_id")
    await _ensure_experiment(request, project_id, experiment_id)
    body = await read_json(request)
    body["experiment_id"] = experiment_id
    body["project_id"] = project_id
    body["created_by"] = user.user_id
    idempotency_service = await get_idempotency_service(request)
    idempotency_key = request.headers.get(IDEMPOTENCY_HEADER)
    try:
        dto = RunCreateDTO.model_validate(body)
    except ValidationError as exc:
        raise web.HTTPBadRequest(text=exc.json()) from exc
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
    service = await get_run_service(request)
    try:
        run = await service.create_run(dto)
    except ScopeMismatchError as exc:
        raise web.HTTPForbidden(text=str(exc)) from exc
    except InvalidStatusTransitionError as exc:
        raise web.HTTPBadRequest(text=str(exc)) from exc
    response_payload = _run_response(run)
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


@routes.get("/api/v1/runs/{run_id}")
async def get_run(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_project_access(user, project_id)
    run_id = parse_uuid(request.match_info["run_id"], "run_id")
    service = await get_run_service(request)
    try:
        run = await service.get_run(project_id, run_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    ensure_project_access(user, run.project_id)
    return web.json_response(_run_response(run))


@routes.patch("/api/v1/runs/{run_id}")
async def update_run(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(
        user, request.rel_url.query.get("project_id"), require_role=("owner", "editor")
    )
    run_id = parse_uuid(request.match_info["run_id"], "run_id")
    body = await read_json(request)
    try:
        dto = RunUpdateDTO.model_validate(body)
    except ValidationError as exc:
        raise web.HTTPBadRequest(text=exc.json()) from exc
    service = await get_run_service(request)
    before = None
    if dto.status is not None:
        try:
            before = await service.get_run(project_id, run_id)
        except NotFoundError as exc:
            raise web.HTTPNotFound(text=str(exc)) from exc
    try:
        run = await service.update_run(project_id, run_id, dto)
    except InvalidStatusTransitionError as exc:
        raise web.HTTPBadRequest(text=str(exc)) from exc
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    if dto.status is not None:
        actor_role = user.project_roles.get(project_id)
        if actor_role and before is not None:
            audit = await get_run_event_service(request)
            await audit.record_event(
                run_id=run.id,
                event_type=EVENT_RUN_STATUS_CHANGED,
                actor_id=user.user_id,
                actor_role=actor_role,
                payload={
                    "from": before.status.value,
                    "to": run.status.value,
                },
            )
        event_type: str | None = None
        if dto.status == RunStatus.RUNNING:
            event_type = EVENT_RUN_STARTED
        elif dto.status in (RunStatus.SUCCEEDED, RunStatus.FAILED):
            event_type = EVENT_RUN_FINISHED
        elif dto.status == RunStatus.ARCHIVED:
            event_type = EVENT_RUN_ARCHIVED
        if event_type:
            webhooks = await get_webhook_service(request)
            await webhooks.emit(
                project_id=project_id,
                event_type=event_type,
                payload={
                    "run_id": str(run.id),
                    "experiment_id": str(run.experiment_id),
                    "status": run.status.value,
                },
            )
    return web.json_response(_run_response(run))


@routes.post("/api/v1/runs:batch-status")
async def batch_update_status(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(
        user, request.rel_url.query.get("project_id"), require_role=("owner", "editor")
    )
    body = await read_json(request)
    run_ids_raw = body.get("run_ids")
    status_value = body.get("status")
    if not isinstance(run_ids_raw, list) or not run_ids_raw:
        raise web.HTTPBadRequest(text="run_ids must be a non-empty list")
    if status_value is None:
        raise web.HTTPBadRequest(text="status is required")
    try:
        status = RunStatus(status_value)
    except ValueError as exc:
        raise web.HTTPBadRequest(text="Invalid status value") from exc
    run_ids = [parse_uuid(value, "run_id") for value in run_ids_raw]
    service = await get_run_service(request)
    actor_role = user.project_roles.get(project_id)
    before_status: dict[UUID, str] = {}
    if actor_role:
        for rid in run_ids:
            try:
                current = await service.get_run(project_id, rid)
            except NotFoundError as exc:
                raise web.HTTPNotFound(text=str(exc)) from exc
            before_status[rid] = current.status.value
    try:
        updated_runs = await service.batch_update_status(project_id, run_ids, status)
    except InvalidStatusTransitionError as exc:
        raise web.HTTPBadRequest(text=str(exc)) from exc
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    if actor_role:
        audit = await get_run_event_service(request)
        for run in updated_runs:
            old = before_status.get(run.id)
            if old is None:
                continue
            await audit.record_event(
                run_id=run.id,
                event_type=EVENT_RUN_STATUS_CHANGED,
                actor_id=user.user_id,
                actor_role=actor_role,
                payload={"from": old, "to": run.status.value},
            )
    event_type: str | None = None
    if status == RunStatus.RUNNING:
        event_type = EVENT_RUN_STARTED
    elif status in (RunStatus.SUCCEEDED, RunStatus.FAILED):
        event_type = EVENT_RUN_FINISHED
    elif status == RunStatus.ARCHIVED:
        event_type = EVENT_RUN_ARCHIVED
    if event_type:
        webhooks = await get_webhook_service(request)
        for run in updated_runs:
            await webhooks.emit(
                project_id=project_id,
                event_type=event_type,
                payload={
                    "run_id": str(run.id),
                    "experiment_id": str(run.experiment_id),
                    "status": run.status.value,
                },
            )
    response_runs = [_run_response(run) for run in updated_runs]
    return web.json_response({"runs": response_runs})


@routes.post("/api/v1/runs:bulk-tags")
async def bulk_update_tags(request: web.Request):
    """
    Bulk tag operations for runs within a project.

    Body:
      - run_ids: [uuid, ...] (required)
      - set_tags: [str, ...] (optional, may be empty to clear)
      - add_tags: [str, ...] (optional)
      - remove_tags: [str, ...] (optional)

    Exactly one mode:
      - set_tags
      - add/remove (at least one non-empty)
    """
    user = await require_current_user(request)
    project_id = resolve_project_id(
        user, request.rel_url.query.get("project_id"), require_role=("owner", "editor")
    )
    body = await read_json(request)

    run_ids_raw = body.get("run_ids")
    if not isinstance(run_ids_raw, list) or not run_ids_raw:
        raise web.HTTPBadRequest(text="run_ids must be a non-empty list")
    run_ids = [parse_uuid(value, "run_id") for value in run_ids_raw]

    def _normalize_tags(raw: object) -> list[str]:
        if raw is None:
            return []
        if not isinstance(raw, list):
            raise web.HTTPBadRequest(text="tags must be a list")
        tags: list[str] = []
        for item in raw:
            if not isinstance(item, str):
                raise web.HTTPBadRequest(text="tags must be a list of strings")
            t = item.strip()
            if t:
                tags.append(t)
        # de-dup but keep stable order for user input
        return list(dict.fromkeys(tags))

    set_tags_raw = body.get("set_tags", None)
    add_tags_raw = body.get("add_tags", None)
    remove_tags_raw = body.get("remove_tags", None)

    has_set = set_tags_raw is not None
    has_add = add_tags_raw is not None
    has_remove = remove_tags_raw is not None

    if has_set and (has_add or has_remove):
        raise web.HTTPBadRequest(text="set_tags cannot be combined with add_tags/remove_tags")

    service = await get_run_service(request)
    try:
        if has_set:
            updated = await service.bulk_update_tags(
                project_id, run_ids, set_tags=_normalize_tags(set_tags_raw)
            )
        else:
            add_tags = _normalize_tags(add_tags_raw)
            remove_tags = _normalize_tags(remove_tags_raw)
            if not add_tags and not remove_tags:
                raise web.HTTPBadRequest(text="add_tags or remove_tags is required")
            updated = await service.bulk_update_tags(
                project_id, run_ids, add_tags=add_tags, remove_tags=remove_tags
            )
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc

    actor_role = user.project_roles.get(project_id)
    if actor_role:
        audit = await get_run_event_service(request)
        for run in updated:
            await audit.record_event(
                run_id=run.id,
                event_type=EVENT_RUN_TAGS_UPDATED,
                actor_id=user.user_id,
                actor_role=actor_role,
                payload={
                    "set_tags": _normalize_tags(set_tags_raw) if has_set else None,
                    "add_tags": _normalize_tags(add_tags_raw) if not has_set else None,
                    "remove_tags": _normalize_tags(remove_tags_raw) if not has_set else None,
                },
            )

    return web.json_response({"runs": [_run_response(run) for run in updated]})


@routes.get("/api/v1/runs/{run_id}/events")
async def list_run_events(request: web.Request):
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_project_access(user, project_id)
    run_id = parse_uuid(request.match_info["run_id"], "run_id")
    run_service = await get_run_service(request)
    try:
        run = await run_service.get_run(project_id, run_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    ensure_project_access(user, run.project_id)

    audit = await get_run_event_service(request)
    limit, offset = pagination_params(request)
    events, total = await audit.list_events(run_id, limit=limit, offset=offset)
    payload = paginated_response(
        [evt.model_dump(mode="json") for evt in events],
        limit=limit,
        offset=offset,
        key="events",
        total=total,
    )
    return web.json_response(payload)
