"""Metrics ingestion and query endpoints."""
from __future__ import annotations

from aiohttp import web
from pydantic import ValidationError

from experiment_service.api.utils import parse_uuid, read_json
from experiment_service.core.exceptions import NotFoundError
from experiment_service.domain.dto import RunMetricPointDTO
from experiment_service.services.dependencies import (
    get_metrics_service,
    require_current_user,
    resolve_project_id,
)

routes = web.RouteTableDef()


@routes.post("/api/v1/runs/{run_id}/metrics")
async def ingest_metrics(request: web.Request):
    """Ingest metrics payloads per spec."""
    user = await require_current_user(request)
    run_id = parse_uuid(request.match_info["run_id"], "run_id")
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    body = await read_json(request)
    metrics_payload = body.get("metrics")
    if not isinstance(metrics_payload, list):
        raise web.HTTPBadRequest(text="metrics array is required")
    points: list[RunMetricPointDTO] = []
    for item in metrics_payload:
        try:
            points.append(RunMetricPointDTO.model_validate(item))
        except ValidationError as exc:
            raise web.HTTPBadRequest(text=exc.json()) from exc
    if not points:
        raise web.HTTPBadRequest(text="metrics array must not be empty")
    service = await get_metrics_service(request)
    try:
        accepted = await service.ingest_metrics(project_id, run_id, points)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.json_response({"status": "accepted", "accepted": accepted}, status=202)


def _parse_optional_int(value: str | None) -> int | None:
    if value is None:
        return None
    try:
        return int(value)
    except ValueError as exc:
        raise web.HTTPBadRequest(text="step filters must be integers") from exc


@routes.get("/api/v1/runs/{run_id}/metrics")
async def query_metrics(request: web.Request):
    """Query metrics with filters and smoothing params."""
    user = await require_current_user(request)
    run_id = parse_uuid(request.match_info["run_id"], "run_id")
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    name = request.rel_url.query.get("name")
    from_step = _parse_optional_int(request.rel_url.query.get("from_step"))
    to_step = _parse_optional_int(request.rel_url.query.get("to_step"))
    service = await get_metrics_service(request)
    try:
        payload = await service.query_metrics(
            project_id,
            run_id,
            name=name,
            from_step=from_step,
            to_step=to_step,
        )
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.json_response(payload)
