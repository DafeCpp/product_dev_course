"""Experiment endpoints skeleton."""
from __future__ import annotations

from aiohttp import web

routes = web.RouteTableDef()


@routes.get("/api/v1/experiments")
async def list_experiments(request: web.Request):
    """List experiments with filters per spec."""
    raise web.HTTPNotImplemented(reason="Not implemented")


@routes.post("/api/v1/experiments")
async def create_experiment(request: web.Request):
    """Create experiment within project scope."""
    raise web.HTTPNotImplemented(reason="Not implemented")


@routes.get("/api/v1/experiments/{experiment_id}")
async def get_experiment(request: web.Request):
    """Retrieve experiment by id."""
    raise web.HTTPNotImplemented(reason="Not implemented")


@routes.patch("/api/v1/experiments/{experiment_id}")
async def update_experiment(request: web.Request):
    """Update experiment metadata/status."""
    raise web.HTTPNotImplemented(reason="Not implemented")


@routes.post("/api/v1/experiments/{experiment_id}/archive")
async def archive_experiment(request: web.Request):
    """Archive experiment respecting invariants."""
    raise web.HTTPNotImplemented(reason="Not implemented")


@routes.delete("/api/v1/experiments/{experiment_id}")
async def delete_experiment(request: web.Request):
    """Delete experiment when allowed by spec rules."""
    raise web.HTTPNotImplemented(reason="Not implemented")


