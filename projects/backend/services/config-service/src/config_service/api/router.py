"""Route registration."""
from __future__ import annotations

from aiohttp import web

from config_service.api.routes.bulk import routes as bulk_routes
from config_service.api.routes.configs import routes as config_routes
from config_service.api.routes.schemas import routes as schema_routes


def setup_routes(app: web.Application) -> None:
    app.add_routes(config_routes)
    app.add_routes(bulk_routes)
    app.add_routes(schema_routes)
