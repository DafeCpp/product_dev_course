"""Run endpoints skeleton."""
from __future__ import annotations

from aiohttp import web

routes = web.RouteTableDef()


@routes.get("/api/v1/experiments/{experiment_id}/runs")
async def list_runs(request: web.Request):
    """List runs of experiment with filters/batch support."""
    raise web.HTTPNotImplemented(reason="Not implemented")


@routes.post("/api/v1/experiments/{experiment_id}/runs")
async def create_run(request: web.Request):
    """Create a run under experiment with git sha/env info."""
    raise web.HTTPNotImplemented(reason="Not implemented")


@routes.get("/api/v1/runs/{run_id}")
async def get_run(request: web.Request):
    """Return run by id."""
    raise web.HTTPNotImplemented(reason="Not implemented")


@routes.patch("/api/v1/runs/{run_id}")
async def update_run(request: web.Request):
    """Update run status/metadata."""
    raise web.HTTPNotImplemented(reason="Not implemented")


@routes.post("/api/v1/runs:batch-status")
async def batch_update_status(request: web.Request):
    """Batch status mutation endpoint required by roadmap."""
    raise web.HTTPNotImplemented(reason="Not implemented")


