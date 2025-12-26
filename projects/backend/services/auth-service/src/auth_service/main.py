"""aiohttp application entrypoint."""
from __future__ import annotations

from aiohttp import web
from aiohttp_cors import setup as cors_setup, ResourceOptions

from auth_service.api.routes.auth import setup_routes
from auth_service.db.pool import close_pool, init_pool
from auth_service.settings import settings


async def healthcheck(request: web.Request) -> web.Response:
    """Health check endpoint."""
    return web.json_response(
        {"status": "ok", "service": settings.app_name, "env": settings.env}
    )


def create_app() -> web.Application:
    """Create aiohttp application."""
    app = web.Application()

    # Configure CORS
    cors = cors_setup(
        app,
        defaults={
            origin: ResourceOptions(
                allow_credentials=True,
                expose_headers="*",
                allow_headers="*",
                allow_methods="*",
            )
            for origin in settings.cors_allowed_origins_list
        },
    )

    # Setup routes
    app.router.add_get("/health", healthcheck)
    setup_routes(app)

    # Setup database pool
    app.on_startup.append(init_pool)
    app.on_cleanup.append(close_pool)

    # Add CORS to all routes
    for route in list(app.router.routes()):
        cors.add(route)

    return app


def main() -> None:
    """Run the application."""
    web.run_app(create_app(), host=settings.host, port=settings.port)


if __name__ == "__main__":
    main()

