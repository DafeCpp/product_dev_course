"""API router: registers all route modules."""
from __future__ import annotations

from aiohttp import web

from script_service.api.routes import executions, scripts

ROUTE_MODULES = [scripts, executions]


def setup_routes(app: web.Application) -> None:
    """Attach domain routes to the aiohttp application."""
    for module in ROUTE_MODULES:
        app.add_routes(module.routes)
