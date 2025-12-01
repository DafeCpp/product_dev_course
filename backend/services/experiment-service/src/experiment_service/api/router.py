"""API router composition for aiohttp."""
from __future__ import annotations

from aiohttp import web

from experiment_service.api.routes import (
    artifacts,
    capture_sessions,
    conversion_profiles,
    experiments,
    metrics,
    runs,
    sensors,
    telemetry,
)

ROUTE_MODULES = [
    experiments,
    runs,
    capture_sessions,
    metrics,
    sensors,
    conversion_profiles,
    artifacts,
    telemetry,
]


def setup_routes(app: web.Application) -> None:
    """Attach domain routes to the aiohttp application."""
    for module in ROUTE_MODULES:
        app.add_routes(module.routes)

