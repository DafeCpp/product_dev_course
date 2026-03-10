"""aiohttp application entrypoint."""
from __future__ import annotations

from pathlib import Path
from typing import Any

from aiohttp import web

from backend_common.aiohttp_app import add_cors_to_routes, add_healthcheck, create_base_app
from backend_common.db.migrations import create_migration_runner
from backend_common.logging_config import configure_logging

from auth_service.api.routes.auth import setup_routes as setup_auth_routes
from auth_service.api.routes.permissions import setup_routes as setup_permissions_routes
from auth_service.api.routes.projects import setup_routes as setup_project_routes
from auth_service.api.routes.project_roles import setup_routes as setup_project_roles_routes
from auth_service.api.routes.system_roles import setup_routes as setup_system_roles_routes
from backend_common.db.pool import close_pool_service as close_pool, init_pool_service
from auth_service.settings import settings


async def init_pool(_app: Any = None) -> None:
    """Initialize pool with service settings."""
    await init_pool_service(_app, settings)

# Configure structured logging
configure_logging()


MIGRATIONS_PATHS = [
    Path(__file__).resolve().parent.parent.parent / "migrations",  # /app/migrations in container
    Path("/app/migrations"),  # Absolute path in container
    Path(__file__).resolve().parent.parent.parent.parent / "migrations",  # Local development
]

apply_migrations_on_startup = create_migration_runner(
    settings,
    MIGRATIONS_PATHS,
    create_db_hint="make auth-create-db",
)


def create_app() -> web.Application:
    """Create aiohttp application."""
    app, cors = create_base_app(settings)

    # Setup routes
    add_healthcheck(app, settings)
    setup_auth_routes(app)
    setup_project_routes(app)
    setup_permissions_routes(app)
    setup_system_roles_routes(app)
    setup_project_roles_routes(app)

    # Setup database pool and migrations
    app.on_startup.append(init_pool)
    app.on_startup.append(apply_migrations_on_startup)
    app.on_cleanup.append(close_pool)

    # Add CORS to all routes
    add_cors_to_routes(app, cors)

    return app


def main() -> None:
    """Run the application."""
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

