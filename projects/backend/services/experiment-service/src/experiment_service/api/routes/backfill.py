"""Conversion backfill task endpoints."""
from __future__ import annotations

from aiohttp import web

from experiment_service.api.utils import paginated_response, pagination_params, parse_uuid
from experiment_service.core.exceptions import NotFoundError
from experiment_service.services.dependencies import (
    ensure_permission,
    ensure_project_context,
    get_backfill_service,
    require_current_user,
)

routes = web.RouteTableDef()


def _serialize_task(task: dict) -> dict:
    """Convert asyncpg Record / dict values to JSON-safe types."""
    result = {}
    for key, value in task.items():
        if hasattr(value, "isoformat"):
            result[key] = value.isoformat()
        elif hasattr(value, "hex"):
            # UUID
            result[key] = str(value)
        else:
            result[key] = value
    return result


@routes.post("/api/v1/sensors/{sensor_id}/backfill")
async def start_backfill(request: web.Request):
    user = await require_current_user(request)
    project_id = ensure_project_context(user)
    ensure_permission(user, "runs.create")
    sensor_id = parse_uuid(request.match_info["sensor_id"], "sensor_id")
    service = await get_backfill_service(request)
    try:
        task = await service.start_backfill(
            project_id, sensor_id, created_by=user.user_id
        )
    except NotFoundError as exc:
        raise web.HTTPNotFound(text="Resource not found") from exc
    return web.json_response(_serialize_task(task), status=201)


@routes.get("/api/v1/sensors/{sensor_id}/backfill")
async def list_backfill_tasks(request: web.Request):
    user = await require_current_user(request)
    project_id = ensure_project_context(user)
    ensure_permission(user, "experiments.view")
    sensor_id = parse_uuid(request.match_info["sensor_id"], "sensor_id")
    service = await get_backfill_service(request)
    limit, offset = pagination_params(request)
    tasks, total = await service.list_tasks(project_id, sensor_id, limit=limit, offset=offset)
    payload = paginated_response(
        [_serialize_task(t) for t in tasks],
        limit=limit,
        offset=offset,
        key="backfill_tasks",
        total=total,
    )
    return web.json_response(payload)


@routes.get("/api/v1/sensors/{sensor_id}/backfill/{task_id}")
async def get_backfill_task(request: web.Request):
    user = await require_current_user(request)
    project_id = ensure_project_context(user)
    ensure_permission(user, "experiments.view")
    task_id = parse_uuid(request.match_info["task_id"], "task_id")
    service = await get_backfill_service(request)
    try:
        task = await service.get_task(project_id, task_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text="Resource not found") from exc
    return web.json_response(_serialize_task(task))
