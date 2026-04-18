"""Metrics ingestion and query endpoints."""
from __future__ import annotations

from aiohttp import web
from pydantic import ValidationError

from experiment_service.api.utils import parse_uuid, read_json
from experiment_service.core.exceptions import NotFoundError
from experiment_service.domain.dto import RunMetricPointDTO
from experiment_service.services.dependencies import (
    ensure_permission,
    get_metrics_service,
    require_current_user,
    resolve_project_id,
)

routes = web.RouteTableDef()


def _parse_optional_int(value: str | None, field: str = "parameter") -> int | None:
    if value is None:
        return None
    try:
        return int(value)
    except ValueError as exc:
        raise web.HTTPBadRequest(text=f"{field} must be an integer") from exc


def _parse_names(raw: str | None) -> list[str] | None:
    if not raw:
        return None
    return [n.strip() for n in raw.split(",") if n.strip()]


@routes.post("/api/v1/runs/{run_id}/metrics")
async def ingest_metrics(request: web.Request) -> web.Response:
    """Ingest metrics batch. Requires editor+ permission."""
    user = await require_current_user(request)
    ensure_permission(user, "metrics.write")
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
    except ValueError as exc:
        raise web.HTTPBadRequest(text=str(exc)) from exc
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.json_response({"status": "accepted", "accepted": accepted}, status=202)


@routes.get("/api/v1/runs/{run_id}/metrics")
async def query_metrics(request: web.Request) -> web.Response:
    """Query metrics with filters, pagination and ordering."""
    user = await require_current_user(request)
    ensure_permission(user, "experiments.view")
    run_id = parse_uuid(request.match_info["run_id"], "run_id")
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))

    name = request.rel_url.query.get("name")
    names = _parse_names(request.rel_url.query.get("names"))
    from_step = _parse_optional_int(request.rel_url.query.get("from_step"), "from_step")
    to_step = _parse_optional_int(request.rel_url.query.get("to_step"), "to_step")
    order = request.rel_url.query.get("order", "asc")
    if order not in ("asc", "desc"):
        raise web.HTTPBadRequest(text="order must be 'asc' or 'desc'")

    limit_raw = _parse_optional_int(request.rel_url.query.get("limit"), "limit")
    limit = limit_raw if limit_raw is not None else 1000
    offset = _parse_optional_int(request.rel_url.query.get("offset"), "offset") or 0

    service = await get_metrics_service(request)
    try:
        payload = await service.query_metrics(
            project_id,
            run_id,
            name=name,
            names=names,
            from_step=from_step,
            to_step=to_step,
            order=order,
            limit=limit,
            offset=offset,
        )
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.json_response(payload)


@routes.get("/api/v1/runs/{run_id}/metrics/summary")
async def metrics_summary(request: web.Request) -> web.Response:
    """Return last value, min/avg/max for each metric of the run."""
    user = await require_current_user(request)
    ensure_permission(user, "experiments.view")
    run_id = parse_uuid(request.match_info["run_id"], "run_id")
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    names = _parse_names(request.rel_url.query.get("names"))

    service = await get_metrics_service(request)
    try:
        payload = await service.get_summary(project_id, run_id, names=names)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.json_response(payload)


@routes.get("/api/v1/runs/{run_id}/metrics/aggregations")
async def metrics_aggregations(request: web.Request) -> web.Response:
    """Return step-bucketed min/avg/max aggregations for selected metrics."""
    user = await require_current_user(request)
    ensure_permission(user, "experiments.view")
    run_id = parse_uuid(request.match_info["run_id"], "run_id")
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))

    names_raw = request.rel_url.query.get("names")
    if not names_raw:
        raise web.HTTPBadRequest(text="names query parameter is required")
    names = [n.strip() for n in names_raw.split(",") if n.strip()]
    if not names:
        raise web.HTTPBadRequest(text="names must contain at least one metric name")

    from_step = _parse_optional_int(request.rel_url.query.get("from_step"), "from_step")
    to_step = _parse_optional_int(request.rel_url.query.get("to_step"), "to_step")
    bucket_size = _parse_optional_int(request.rel_url.query.get("bucket_size"), "bucket_size")
    if bucket_size is not None and bucket_size < 1:
        raise web.HTTPBadRequest(text="bucket_size must be >= 1")

    service = await get_metrics_service(request)
    try:
        payload = await service.get_aggregations(
            project_id,
            run_id,
            names=names,
            from_step=from_step,
            to_step=to_step,
            bucket_size=bucket_size,
        )
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.json_response(payload)
