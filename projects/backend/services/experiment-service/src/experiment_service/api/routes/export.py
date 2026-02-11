"""Data export endpoints (CSV / JSON)."""
from __future__ import annotations

import csv
import io
import json
from datetime import datetime

from aiohttp import web

from experiment_service.api.utils import (
    parse_datetime,
    parse_tags_filter,
    parse_uuid,
)
from experiment_service.domain.enums import ExperimentStatus, RunStatus
from experiment_service.domain.models import Experiment, Run
from experiment_service.services.dependencies import (
    ensure_project_access,
    get_experiment_service,
    get_run_service,
    require_current_user,
    resolve_project_id,
)

routes = web.RouteTableDef()

EXPORT_LIMIT = 5000  # max rows per export request


def _format_dt(dt: datetime | None) -> str:
    if dt is None:
        return ""
    return dt.isoformat()


def _experiments_to_csv(experiments: list[Experiment]) -> str:
    buf = io.StringIO()
    writer = csv.writer(buf)
    writer.writerow([
        "id", "project_id", "name", "description", "experiment_type",
        "status", "tags", "owner_id", "created_at", "updated_at", "archived_at",
    ])
    for exp in experiments:
        writer.writerow([
            str(exp.id),
            str(exp.project_id),
            exp.name,
            exp.description or "",
            exp.experiment_type or "",
            exp.status.value,
            ",".join(exp.tags),
            str(exp.owner_id),
            _format_dt(exp.created_at),
            _format_dt(exp.updated_at),
            _format_dt(exp.archived_at),
        ])
    return buf.getvalue()


def _experiments_to_json(experiments: list[Experiment]) -> str:
    return json.dumps(
        [exp.model_dump(mode="json") for exp in experiments],
        ensure_ascii=False,
        indent=2,
    )


def _runs_to_csv(runs: list[Run]) -> str:
    buf = io.StringIO()
    writer = csv.writer(buf)
    writer.writerow([
        "id", "experiment_id", "project_id", "name", "status",
        "tags", "git_sha", "env", "notes",
        "started_at", "finished_at", "duration_seconds",
        "created_by", "created_at", "updated_at",
    ])
    for run in runs:
        writer.writerow([
            str(run.id),
            str(run.experiment_id),
            str(run.project_id),
            run.name or "",
            run.status.value,
            ",".join(run.tags),
            run.git_sha or "",
            run.env or "",
            run.notes or "",
            _format_dt(run.started_at),
            _format_dt(run.finished_at),
            run.duration_seconds if run.duration_seconds is not None else "",
            str(run.created_by),
            _format_dt(run.created_at),
            _format_dt(run.updated_at),
        ])
    return buf.getvalue()


def _runs_to_json(runs: list[Run]) -> str:
    return json.dumps(
        [run.model_dump(mode="json") for run in runs],
        ensure_ascii=False,
        indent=2,
    )


@routes.get("/api/v1/experiments/export")
async def export_experiments(request: web.Request):
    """Export experiments for a project as CSV or JSON.

    Query params:
      - project_id (required or from header)
      - format: csv | json (default csv)
      - status, tags, created_after, created_before — same filters as list
    """
    user = await require_current_user(request)
    project_id_query = request.rel_url.query.get("project_id")
    if project_id_query:
        project_id = resolve_project_id(user, project_id_query)
    elif user.active_project_id:
        ensure_project_access(user, user.active_project_id)
        project_id = user.active_project_id
    else:
        raise web.HTTPBadRequest(text="project_id is required")

    fmt = request.rel_url.query.get("format", "csv").lower()
    if fmt not in ("csv", "json"):
        raise web.HTTPBadRequest(text="format must be csv or json")

    status_raw = request.rel_url.query.get("status")
    status = ExperimentStatus(status_raw) if status_raw else None
    tags = parse_tags_filter(request.rel_url.query.get("tags"))
    created_after = parse_datetime(request.rel_url.query.get("created_after"), "created_after")
    created_before = parse_datetime(request.rel_url.query.get("created_before"), "created_before")

    service = await get_experiment_service(request)
    experiments, _total = await service.list_experiments(
        project_id,
        limit=EXPORT_LIMIT,
        offset=0,
        status=status,
        tags=tags,
        created_after=created_after,
        created_before=created_before,
    )

    if fmt == "json":
        body = _experiments_to_json(experiments)
        content_type = "application/json"
        filename = "experiments.json"
    else:
        body = _experiments_to_csv(experiments)
        content_type = "text/csv"
        filename = "experiments.csv"

    return web.Response(
        text=body,
        content_type=content_type,
        headers={
            "Content-Disposition": f'attachment; filename="{filename}"',
        },
    )


@routes.get("/api/v1/experiments/{experiment_id}/runs/export")
async def export_runs(request: web.Request):
    """Export runs for an experiment as CSV or JSON.

    Query params:
      - project_id (required or from header)
      - format: csv | json (default csv)
      - status, tags, created_after, created_before — same filters as list
    """
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_project_access(user, project_id)
    experiment_id = parse_uuid(request.match_info["experiment_id"], "experiment_id")

    fmt = request.rel_url.query.get("format", "csv").lower()
    if fmt not in ("csv", "json"):
        raise web.HTTPBadRequest(text="format must be csv or json")

    status_raw = request.rel_url.query.get("status")
    status = RunStatus(status_raw) if status_raw else None
    tags = parse_tags_filter(request.rel_url.query.get("tags"))
    created_after = parse_datetime(request.rel_url.query.get("created_after"), "created_after")
    created_before = parse_datetime(request.rel_url.query.get("created_before"), "created_before")

    service = await get_run_service(request)
    runs, _total = await service.list_runs_for_experiment(
        project_id,
        experiment_id,
        limit=EXPORT_LIMIT,
        offset=0,
        status=status,
        tags=tags,
        created_after=created_after,
        created_before=created_before,
    )

    if fmt == "json":
        body = _runs_to_json(runs)
        content_type = "application/json"
        filename = f"runs_{experiment_id}.json"
    else:
        body = _runs_to_csv(runs)
        content_type = "text/csv"
        filename = f"runs_{experiment_id}.csv"

    return web.Response(
        text=body,
        content_type=content_type,
        headers={
            "Content-Disposition": f'attachment; filename="{filename}"',
        },
    )
