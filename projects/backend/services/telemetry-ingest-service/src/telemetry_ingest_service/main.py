"""aiohttp application entrypoint."""
from __future__ import annotations

import asyncio
from pathlib import Path
from typing import Any

from aiohttp import web

from backend_common.aiohttp_app import (
    add_cors_to_routes,
    add_openapi_spec,
    create_base_app,
)
from backend_common.metrics import metrics_handler, metrics_middleware
from backend_common.middleware.error_handler import error_handling_middleware
from backend_common.db.pool import close_pool_service as close_pool, init_pool_service
from backend_common.logging_config import configure_logging

from telemetry_ingest_service.api.routes.health import health_routes
from telemetry_ingest_service.api.routes.telemetry import routes as telemetry_routes
from telemetry_ingest_service.api.routes.ws_ingest import ws_routes
from telemetry_ingest_service.settings import settings
from telemetry_ingest_service.workers.spool_flush import run_spool_flush_worker

# Configure structured logging
configure_logging()

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
OPENAPI_PATH = PROJECT_ROOT / "openapi" / "openapi.yaml"


async def init_pool(_app: Any = None) -> None:
    await init_pool_service(_app, settings)


async def _start_spool_worker(app: web.Application) -> None:
    if settings.spool_enabled:
        app["_spool_worker"] = asyncio.ensure_future(run_spool_flush_worker())


async def _stop_spool_worker(app: web.Application) -> None:
    task: asyncio.Task | None = app.get("_spool_worker")
    if task is not None:
        task.cancel()
        try:
            await task
        except asyncio.CancelledError:
            pass


def create_app() -> web.Application:
    app, cors = create_base_app(settings)
    app.middlewares.append(error_handling_middleware)  # type: ignore[arg-type]
    app.middlewares.append(metrics_middleware("telemetry-ingest-service"))  # type: ignore[arg-type]

    app.add_routes(health_routes)
    add_openapi_spec(app, OPENAPI_PATH)
    app.add_routes(telemetry_routes)
    app.add_routes(ws_routes)
    app.router.add_get("/metrics", metrics_handler)

    app.on_startup.append(init_pool)
    app.on_startup.append(_start_spool_worker)
    app.on_cleanup.append(_stop_spool_worker)
    app.on_cleanup.append(close_pool)

    add_cors_to_routes(app, cors)

    return app


def main() -> None:
    web.run_app(create_app(), host=settings.host, port=settings.port, access_log=None)


if __name__ == "__main__":
    main()
