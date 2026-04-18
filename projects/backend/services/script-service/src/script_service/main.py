"""aiohttp application entrypoint for script-service."""
from __future__ import annotations

from pathlib import Path
from typing import Any

from aiohttp import web

from backend_common.aiohttp_app import add_cors_to_routes, create_base_app
from backend_common.db.migrations import create_migration_runner
from backend_common.db.pool import close_pool_service as close_pool, init_pool_service
from backend_common.logging_config import configure_logging

from script_service.api.router import setup_routes
from script_service.api.routes.health import health_routes
from script_service.settings import settings

# Configure structured logging early
configure_logging()

MIGRATIONS_PATHS = [
    Path(__file__).resolve().parent.parent.parent / "migrations",  # local dev
    Path("/app/migrations"),  # container path
    Path(__file__).resolve().parent.parent.parent.parent / "migrations",
]

apply_migrations_on_startup = create_migration_runner(settings, MIGRATIONS_PATHS)


async def init_pool(_app: Any = None) -> None:
    """Initialize DB pool with service settings."""
    await init_pool_service(_app, settings)


def create_app() -> web.Application:
    app, cors = create_base_app(settings)

    app.add_routes(health_routes)
    setup_routes(app)

    app.on_startup.append(init_pool)
    app.on_startup.append(apply_migrations_on_startup)
    app.on_cleanup.append(close_pool)

    add_cors_to_routes(app, cors)

    return app


def main() -> None:
    web.run_app(
        create_app(),
        host=settings.host,
        port=settings.port,
        access_log=None,
    )


if __name__ == "__main__":
    main()
