"""Metrics ingestion and query endpoints skeleton."""
from __future__ import annotations

from aiohttp import web

routes = web.RouteTableDef()


@routes.post("/api/v1/runs/{run_id}/metrics")
async def ingest_metrics(request: web.Request):
    """Ingest metrics payloads per spec."""
    raise web.HTTPNotImplemented(reason="Not implemented")


@routes.get("/api/v1/runs/{run_id}/metrics")
async def query_metrics(request: web.Request):
    """Query metrics with filters and smoothing params."""
    raise web.HTTPNotImplemented(reason="Not implemented")


