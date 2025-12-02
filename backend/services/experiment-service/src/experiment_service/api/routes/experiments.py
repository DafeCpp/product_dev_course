"""Experiment endpoints."""
# pyright: reportMissingImports=false
from __future__ import annotations

from datetime import datetime, timezone

from aiohttp import web
from pydantic import ValidationError

from experiment_service.api.utils import (
    paginated_response,
    pagination_params,
    parse_uuid,
    read_json,
)
from experiment_service.core.exceptions import NotFoundError
from experiment_service.domain.dto import ExperimentCreateDTO, ExperimentUpdateDTO
from experiment_service.domain.enums import ExperimentStatus
from experiment_service.domain.models import Experiment
from experiment_service.services.dependencies import (
    ensure_project_access,
    get_experiment_service,
    require_current_user,
    resolve_project_id,
)

routes = web.RouteTableDef()


def _experiment_response(experiment: Experiment) -> dict:
    return experiment.model_dump(mode="json")


@routes.get("/api/v1/experiments")
async def list_experiments(request: web.Request):
    user = await require_current_user(request)
    service = await get_experiment_service(request)
    limit, offset = pagination_params(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    experiments, total = await service.list_experiments(project_id, limit=limit, offset=offset)
    payload = paginated_response(
        [_experiment_response(item) for item in experiments],
        limit=limit,
        offset=offset,
        key="experiments",
        total=total,
    )
    return web.json_response(payload)


@routes.post("/api/v1/experiments")
async def create_experiment(request: web.Request):
    user = await require_current_user(request)
    service = await get_experiment_service(request)
    body = await read_json(request)
    project_id = resolve_project_id(
        user, body.get("project_id"), require_role=("owner", "editor")
    )
    body["project_id"] = project_id
    body["owner_id"] = user.user_id
    try:
        dto = ExperimentCreateDTO.model_validate(body)
    except ValidationError as exc:
        raise web.HTTPBadRequest(text=exc.json()) from exc
    experiment = await service.create_experiment(dto)
    return web.json_response(_experiment_response(experiment), status=201)


@routes.get("/api/v1/experiments/{experiment_id}")
async def get_experiment(request: web.Request):
    user = await require_current_user(request)
    service = await get_experiment_service(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    experiment_id = parse_uuid(request.match_info["experiment_id"], "experiment_id")
    try:
        experiment = await service.get_experiment(project_id, experiment_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    ensure_project_access(user, experiment.project_id)
    return web.json_response(_experiment_response(experiment))


@routes.patch("/api/v1/experiments/{experiment_id}")
async def update_experiment(request: web.Request):
    user = await require_current_user(request)
    service = await get_experiment_service(request)
    project_id = resolve_project_id(
        user, request.rel_url.query.get("project_id"), require_role=("owner", "editor")
    )
    experiment_id = parse_uuid(request.match_info["experiment_id"], "experiment_id")
    body = await read_json(request)
    try:
        dto = ExperimentUpdateDTO.model_validate(body)
    except ValidationError as exc:
        raise web.HTTPBadRequest(text=exc.json()) from exc
    try:
        experiment = await service.update_experiment(project_id, experiment_id, dto)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.json_response(_experiment_response(experiment))


@routes.post("/api/v1/experiments/{experiment_id}/archive")
async def archive_experiment(request: web.Request):
    user = await require_current_user(request)
    service = await get_experiment_service(request)
    project_id = resolve_project_id(
        user, request.rel_url.query.get("project_id"), require_role=("owner", "editor")
    )
    experiment_id = parse_uuid(request.match_info["experiment_id"], "experiment_id")
    dto = ExperimentUpdateDTO(
        status=ExperimentStatus.ARCHIVED,
        archived_at=datetime.now(timezone.utc),
    )
    try:
        experiment = await service.update_experiment(project_id, experiment_id, dto)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.json_response(_experiment_response(experiment))


@routes.delete("/api/v1/experiments/{experiment_id}")
async def delete_experiment(request: web.Request):
    user = await require_current_user(request)
    service = await get_experiment_service(request)
    project_id = resolve_project_id(
        user, request.rel_url.query.get("project_id"), require_role=("owner", "editor")
    )
    experiment_id = parse_uuid(request.match_info["experiment_id"], "experiment_id")
    try:
        await service.delete_experiment(project_id, experiment_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.Response(status=204)
