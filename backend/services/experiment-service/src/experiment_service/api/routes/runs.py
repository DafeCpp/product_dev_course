"""Run endpoints."""
# pyright: reportMissingImports=false
from __future__ import annotations

from uuid import UUID

from aiohttp import web
from pydantic import ValidationError

from experiment_service.api.utils import (
    paginated_response,
    pagination_params,
    parse_uuid,
    read_json,
)
from experiment_service.core.exceptions import NotFoundError, ScopeMismatchError
from experiment_service.domain.dto import RunCreateDTO, RunUpdateDTO
from experiment_service.domain.enums import RunStatus
from experiment_service.domain.models import Run
from experiment_service.services.dependencies import (
    ensure_project_access,
    get_experiment_service,
    get_run_service,
    require_current_user,
    resolve_project_id,
)

routes = web.RouteTableDef()


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
    runs, total = await service.list_runs_for_experiment(
        project_id, experiment_id, limit=limit, offset=offset
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
    try:
        dto = RunCreateDTO.model_validate(body)
    except ValidationError as exc:
        raise web.HTTPBadRequest(text=exc.json()) from exc
    service = await get_run_service(request)
    try:
        run = await service.create_run(dto)
    except ScopeMismatchError as exc:
        raise web.HTTPForbidden(text=str(exc)) from exc
    return web.json_response(_run_response(run), status=201)


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
    try:
        run = await service.update_run(project_id, run_id, dto)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
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
    try:
        updated_runs = await service.batch_update_status(project_id, run_ids, status)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    response_runs = [_run_response(run) for run in updated_runs]
    return web.json_response({"runs": response_runs})
