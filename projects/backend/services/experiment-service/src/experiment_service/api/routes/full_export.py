"""Full experiment ZIP export endpoint."""
from __future__ import annotations

from uuid import UUID

from aiohttp import web

from backend_common.db.pool import get_pool_service as get_pool
from experiment_service.api.utils import parse_uuid
from experiment_service.core.exceptions import NotFoundError
from experiment_service.services.dependencies import (
    ensure_permission,
    require_current_user,
    resolve_project_id,
)
from experiment_service.services.full_export import FullExportService

routes = web.RouteTableDef()


@routes.get("/api/v1/experiments/{experiment_id}/export")
async def export_experiment_zip(request: web.Request) -> web.Response:
    """Export a full experiment as a ZIP archive.

    Query params:
      - project_id (required or from X-Project-Id header)
      - format: zip (only accepted value; defaults to zip)

    RBAC: requires ``experiments.view`` permission (viewer+).

    Returns:
      application/zip with filename ``experiment_<id>.zip``.
    """
    user = await require_current_user(request)
    ensure_permission(user, "experiments.view")
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))

    fmt = request.rel_url.query.get("format", "zip").lower()
    if fmt != "zip":
        raise web.HTTPBadRequest(text="format must be zip")

    experiment_id: UUID = parse_uuid(request.match_info["experiment_id"], "experiment_id")

    pool = await get_pool()
    service = FullExportService(pool)

    try:
        archive_bytes = await service.build_zip(project_id, experiment_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc

    filename = f"experiment_{experiment_id}.zip"
    return web.Response(
        body=archive_bytes,
        content_type="application/zip",
        headers={
            "Content-Disposition": f'attachment; filename="{filename}"',
        },
    )
