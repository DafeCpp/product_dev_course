"""API router composition for aiohttp."""
from __future__ import annotations

from aiohttp import web

from experiment_service.api.routes import (
    artifacts,
    backfill,
    capture_sessions,
    comparison,
    conversion_profiles,
    experiments,
    export,
    full_export,
    metrics,
    runs,
    sensors,
    telemetry_export,
    webhooks,
)

ROUTE_MODULES = [
    full_export,
    export,
    telemetry_export,
    experiments,
    runs,
    capture_sessions,
    webhooks,
    metrics,
    comparison,
    sensors,
    conversion_profiles,
    backfill,
    artifacts,
]


def setup_routes(app: web.Application) -> None:
    """Attach domain routes to the aiohttp application."""
    for module in ROUTE_MODULES:
        app.add_routes(module.routes)

