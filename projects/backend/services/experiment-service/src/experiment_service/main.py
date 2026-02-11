"""aiohttp application entrypoint."""
from __future__ import annotations

from pathlib import Path
from typing import Any

from aiohttp import web

from backend_common.aiohttp_app import (
    add_cors_to_routes,
    add_healthcheck,
    add_openapi_spec,
    create_base_app,
)
from backend_common.db.migrations import create_migration_runner
from backend_common.logging_config import configure_logging

from experiment_service.api.router import setup_routes
from backend_common.db.pool import close_pool_service as close_pool, init_pool_service
from experiment_service.workers import start_background_worker, stop_background_worker
from experiment_service.otel import setup_otel, shutdown_otel
from experiment_service.settings import settings
from experiment_service.webhooks_dispatcher import start_webhook_dispatcher, stop_webhook_dispatcher


async def init_pool(_app: Any = None) -> None:
    """Initialize pool with service settings."""
    await init_pool_service(_app, settings)

# Configure structured logging
configure_logging()

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
OPENAPI_PATH = PROJECT_ROOT / "openapi" / "openapi.yaml"

MIGRATIONS_PATHS = [
    Path(__file__).resolve().parent.parent.parent / "migrations",  # /app/migrations in container
    Path("/app/migrations"),  # Absolute path in container
    Path(__file__).resolve().parent.parent.parent.parent / "migrations",  # Local development
]

apply_migrations_on_startup = create_migration_runner(settings, MIGRATIONS_PATHS)


def create_app() -> web.Application:
    app, cors = create_base_app(settings)

    add_healthcheck(app, settings)
    add_openapi_spec(app, OPENAPI_PATH)
    setup_routes(app)

    # OpenTelemetry (auto-instruments aiohttp server; no-op when endpoint not set)
    setup_otel(app)

    app.on_startup.append(init_pool)
    app.on_startup.append(apply_migrations_on_startup)
    app.on_startup.append(start_webhook_dispatcher)
    app.on_startup.append(start_background_worker)
    app.on_cleanup.append(stop_background_worker)
    app.on_cleanup.append(stop_webhook_dispatcher)
    app.on_cleanup.append(shutdown_otel)
    app.on_cleanup.append(close_pool)

    # Add CORS to all routes
    add_cors_to_routes(app, cors)

    return app


def main() -> None:
    # Disable aiohttp access log (we use structlog middleware instead)
    # This prevents duplicate/unstructured logs
    web.run_app(
        create_app(),
        host=settings.host,
        port=settings.port,
        access_log=None,  # Disable default access log
    )


if __name__ == "__main__":
    main()

