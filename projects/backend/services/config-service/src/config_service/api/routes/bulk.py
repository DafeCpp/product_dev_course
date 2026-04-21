"""Bulk config endpoint for SDK polling."""
from __future__ import annotations

from email.utils import formatdate

from aiohttp import web

from config_service.prometheus_metrics import config_bulk_responses_total
from config_service.services.dependencies import get_bulk_service

routes = web.RouteTableDef()


@routes.get("/api/v1/configs/bulk")
async def bulk_configs(request: web.Request) -> web.Response:
    service_name = request.rel_url.query.get("service")
    if not service_name:
        raise web.HTTPBadRequest(reason="service query parameter is required")

    project_id = request.rel_url.query.get("project")

    bulk_svc = await get_bulk_service(request)
    result = await bulk_svc.get_bulk(service_name, project_id)

    if_none_match = request.headers.get("If-None-Match", "").strip()
    if if_none_match and if_none_match == result.etag:
        config_bulk_responses_total.labels(status="304").inc()
        headers: dict[str, str] = {"ETag": result.etag}
        if result.last_modified:
            headers["Last-Modified"] = formatdate(
                result.last_modified.timestamp(), usegmt=True
            )
        return web.Response(status=304, headers=headers)

    config_bulk_responses_total.labels(status="200").inc()
    headers = {"ETag": result.etag}
    if result.last_modified:
        headers["Last-Modified"] = formatdate(
            result.last_modified.timestamp(), usegmt=True
        )
    return web.json_response({"configs": result.configs}, headers=headers)
