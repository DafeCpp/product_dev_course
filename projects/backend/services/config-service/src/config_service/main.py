"""aiohttp application entrypoint."""
from __future__ import annotations

from pathlib import Path
from typing import Any

from aiohttp import web

from backend_common.aiohttp_app import add_cors_to_routes, add_openapi_spec, create_base_app
from backend_common.db.pool import close_pool_service as close_pool, init_pool_service
from backend_common.logging_config import configure_logging
from backend_common.metrics import metrics_handler, metrics_middleware
from backend_common.middleware.error_handler import error_handling_middleware

from config_service.api.router import setup_routes
from config_service.api.routes.health import health_routes
from config_service.otel import setup_otel, shutdown_otel
from config_service.settings import settings
from config_service.workers import start_background_worker, stop_background_worker

configure_logging()

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
OPENAPI_PATH = PROJECT_ROOT / "openapi" / "openapi.yaml"


async def init_pool(_app: Any = None) -> None:
    await init_pool_service(_app, settings)


def create_app() -> web.Application:
    app, cors = create_base_app(settings)
    app.middlewares.append(error_handling_middleware)  # type: ignore[arg-type]
    app.middlewares.append(metrics_middleware("config-service"))

    app.add_routes(health_routes)
    if OPENAPI_PATH.exists():
        add_openapi_spec(app, OPENAPI_PATH)
    setup_routes(app)
    app.router.add_get("/metrics", metrics_handler)

    setup_otel(app)

    app.on_startup.append(init_pool)
    app.on_startup.append(start_background_worker)
    app.on_cleanup.append(stop_background_worker)
    app.on_cleanup.append(shutdown_otel)
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
