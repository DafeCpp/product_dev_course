"""aiohttp application entrypoint."""
from __future__ import annotations

from pathlib import Path
from typing import Any

from aiohttp import web
from aiohttp_cors import ResourceOptions, setup as cors_setup

from backend_common.db.pool import close_pool_service as close_pool, init_pool_service
from backend_common.logging_config import configure_logging
from backend_common.middleware.trace import create_trace_middleware

from telemetry_ingest_service.api.routes.telemetry import routes as telemetry_routes
from telemetry_ingest_service.settings import settings

# Configure structured logging
configure_logging()

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
OPENAPI_PATH = PROJECT_ROOT / "openapi" / "openapi.yaml"


async def init_pool(_app: Any = None) -> None:
    await init_pool_service(_app, settings)


async def healthcheck(_request: web.Request) -> web.Response:
    return web.json_response({"status": "ok", "service": settings.app_name, "env": settings.env})


async def openapi_spec(_request: web.Request) -> web.StreamResponse:
    return web.FileResponse(OPENAPI_PATH, headers={"Content-Type": "application/yaml"})


def create_app() -> web.Application:
    app = web.Application()

    trace_middleware = create_trace_middleware(settings.app_name)
    app.middlewares.append(trace_middleware)

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
    app.add_routes(telemetry_routes)

    app.on_startup.append(init_pool)
    app.on_cleanup.append(close_pool)

    for route in list(app.router.routes()):
        cors.add(route)

    return app


def main() -> None:
    web.run_app(create_app(), host=settings.host, port=settings.port, access_log=None)


if __name__ == "__main__":
    main()

