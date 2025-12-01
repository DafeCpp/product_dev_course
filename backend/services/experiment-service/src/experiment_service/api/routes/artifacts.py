"""Artifact endpoints skeleton."""
from __future__ import annotations

from aiohttp import web

routes = web.RouteTableDef()


@routes.post("/api/v1/runs/{run_id}/artifacts")
async def create_artifact(request: web.Request):
    """Create artifact metadata + presigned upload."""
    raise web.HTTPNotImplemented(reason="Not implemented")


@routes.get("/api/v1/runs/{run_id}/artifacts")
async def list_artifacts(request: web.Request):
    """List artifacts for a run."""
    raise web.HTTPNotImplemented(reason="Not implemented")


@routes.post("/api/v1/runs/{run_id}/artifacts/{artifact_id}/approve")
async def approve_artifact(request: web.Request):
    """Approve artifact with audit note."""
    raise web.HTTPNotImplemented(reason="Not implemented")


