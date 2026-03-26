"""Comparison endpoints: POST/GET /api/v1/experiments/{experiment_id}/compare."""
from __future__ import annotations

from uuid import UUID

from aiohttp import web

from experiment_service.api.utils import parse_uuid, read_json
from experiment_service.core.exceptions import NotFoundError
from experiment_service.services.dependencies import (
    ensure_permission,
    get_metrics_service,
    require_current_user,
    resolve_project_id,
)

routes = web.RouteTableDef()

_DEFAULT_MAX_POINTS = 500
_MIN_MAX_POINTS = 100
_MAX_MAX_POINTS = 2000


def _parse_optional_int(value: str | None, field: str) -> int | None:
    if value is None:
        return None
    try:
        return int(value)
    except ValueError as exc:
        raise web.HTTPBadRequest(text=f"{field} must be an integer") from exc


def _parse_uuid_list(raw: str, field: str) -> list[UUID]:
    parts = [p.strip() for p in raw.split(",") if p.strip()]
    result: list[UUID] = []
    for part in parts:
        try:
            result.append(UUID(part))
        except ValueError as exc:
            raise web.HTTPBadRequest(text=f"Invalid UUID in {field}: {part!r}") from exc
    return result


@routes.post("/api/v1/experiments/{experiment_id}/compare")
async def compare_post(request: web.Request) -> web.Response:
    """Compare metrics across runs. Body: {run_ids, metric_names, from_step?, to_step?, max_points_per_series?}."""
    user = await require_current_user(request)
    ensure_permission(user, "experiments.view")
    experiment_id = parse_uuid(request.match_info["experiment_id"], "experiment_id")
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))

    body = await read_json(request)

    raw_run_ids = body.get("run_ids")
    if not isinstance(raw_run_ids, list) or not raw_run_ids:
        raise web.HTTPBadRequest(text="run_ids must be a non-empty array")
    run_ids: list[UUID] = []
    for item in raw_run_ids:
        try:
            run_ids.append(UUID(str(item)))
        except (ValueError, TypeError) as exc:
            raise web.HTTPBadRequest(text=f"Invalid UUID in run_ids: {item!r}") from exc

    metric_names = body.get("metric_names")
    if not isinstance(metric_names, list) or not metric_names:
        raise web.HTTPBadRequest(text="metric_names must be a non-empty array")
    metric_names = [str(n) for n in metric_names]

    from_step_raw = body.get("from_step")
    to_step_raw = body.get("to_step")
    from_step: int | None = int(from_step_raw) if from_step_raw is not None else None
    to_step: int | None = int(to_step_raw) if to_step_raw is not None else None

    max_points_raw = body.get("max_points_per_series", _DEFAULT_MAX_POINTS)
    try:
        max_points = int(max_points_raw)
    except (TypeError, ValueError) as exc:
        raise web.HTTPBadRequest(text="max_points_per_series must be an integer") from exc
    max_points = max(_MIN_MAX_POINTS, min(_MAX_MAX_POINTS, max_points))

    service = await get_metrics_service(request)
    try:
        payload = await service.compare_runs(
            project_id,
            experiment_id,
            run_ids=run_ids,
            metric_names=metric_names,
            from_step=from_step,
            to_step=to_step,
            max_points_per_series=max_points,
        )
    except ValueError as exc:
        raise web.HTTPBadRequest(text=str(exc)) from exc
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.json_response(payload)


@routes.get("/api/v1/experiments/{experiment_id}/compare")
async def compare_get(request: web.Request) -> web.Response:
    """Compare metrics across runs via query parameters (shareable URL)."""
    user = await require_current_user(request)
    ensure_permission(user, "experiments.view")
    experiment_id = parse_uuid(request.match_info["experiment_id"], "experiment_id")
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))

    run_ids_raw = request.rel_url.query.get("run_ids")
    if not run_ids_raw:
        raise web.HTTPBadRequest(text="run_ids query parameter is required")
    run_ids = _parse_uuid_list(run_ids_raw, "run_ids")

    names_raw = request.rel_url.query.get("names")
    if not names_raw:
        raise web.HTTPBadRequest(text="names query parameter is required")
    metric_names = [n.strip() for n in names_raw.split(",") if n.strip()]
    if not metric_names:
        raise web.HTTPBadRequest(text="names must contain at least one metric name")

    from_step = _parse_optional_int(request.rel_url.query.get("from_step"), "from_step")
    to_step = _parse_optional_int(request.rel_url.query.get("to_step"), "to_step")

    max_points_raw = _parse_optional_int(request.rel_url.query.get("max_points"), "max_points")
    max_points = max_points_raw if max_points_raw is not None else _DEFAULT_MAX_POINTS
    max_points = max(_MIN_MAX_POINTS, min(_MAX_MAX_POINTS, max_points))

    service = await get_metrics_service(request)
    try:
        payload = await service.compare_runs(
            project_id,
            experiment_id,
            run_ids=run_ids,
            metric_names=metric_names,
            from_step=from_step,
            to_step=to_step,
            max_points_per_series=max_points,
        )
    except ValueError as exc:
        raise web.HTTPBadRequest(text=str(exc)) from exc
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.json_response(payload)
