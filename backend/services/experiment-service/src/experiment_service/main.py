"""aiohttp application entrypoint."""
from __future__ import annotations

from pathlib import Path

from aiohttp import web
from aiohttp_cors import setup as cors_setup, ResourceOptions

from experiment_service.api.router import setup_routes
from experiment_service.db.pool import close_pool, init_pool
from experiment_service.settings import settings

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
OPENAPI_PATH = PROJECT_ROOT / "openapi" / "openapi.yaml"


async def healthcheck(request: web.Request) -> web.Response:
    return web.json_response({"status": "ok", "service": settings.app_name, "env": settings.env})


async def openapi_spec(request: web.Request) -> web.StreamResponse:
    return web.FileResponse(OPENAPI_PATH, headers={"Content-Type": "application/yaml"})


def create_app() -> web.Application:
    app = web.Application()

    # Configure CORS first, before adding routes
    cors = cors_setup(
        app,
        defaults={
            origin: ResourceOptions(
                allow_credentials=True,
                expose_headers="*",
                allow_headers="*",
                allow_methods="*",
            )
            for origin in settings.cors_allowed_origins
        },
    )

    app.router.add_get("/health", healthcheck)
    app.router.add_get("/openapi.yaml", openapi_spec)
    setup_routes(app)
    app.on_startup.append(init_pool)
    app.on_cleanup.append(close_pool)

    # Add CORS to all routes
    for route in list(app.router.routes()):
        cors.add(route)

    return app


def main() -> None:
    web.run_app(create_app(), host=settings.host, port=settings.port)


if __name__ == "__main__":
    main()

