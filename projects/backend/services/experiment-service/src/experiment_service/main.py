"""aiohttp application entrypoint."""
from __future__ import annotations

from pathlib import Path
from typing import Any

from aiohttp import ClientSession, ClientTimeout, web

from backend_common.aiohttp_app import (
    add_cors_to_routes,
    add_openapi_spec,
    create_base_app,
)
from backend_common.metrics import metrics_handler, metrics_middleware
from backend_common.middleware.error_handler import error_handling_middleware
from backend_common.logging_config import configure_logging

from experiment_service.api.router import setup_routes
from experiment_service.api.routes.health import health_routes
from backend_common.db.pool import close_pool_service as close_pool, init_pool_service
from experiment_service.middleware.audit import audit_middleware, _AUDIT_CLIENT_KEY
from experiment_service.services.audit_client import AuditClient
from experiment_service.workers import start_background_worker, stop_background_worker
from experiment_service.otel import setup_otel, shutdown_otel
from experiment_service.settings import settings
from experiment_service.webhooks_dispatcher import start_webhook_dispatcher, stop_webhook_dispatcher
from backend_common.script_runner import ScriptRunner

_AUDIT_SESSION_KEY = "audit_http_session"
_SCRIPT_RUNNER_KEY = "script_runner"


async def init_pool(_app: Any = None) -> None:
    """Initialize pool with service settings."""
    await init_pool_service(_app, settings)

# Configure structured logging
configure_logging()

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
OPENAPI_PATH = PROJECT_ROOT / "openapi" / "openapi.yaml"


async def start_audit_client(app: web.Application) -> None:
    session = ClientSession(timeout=ClientTimeout(total=5.0))
    app[_AUDIT_SESSION_KEY] = session
    app[_AUDIT_CLIENT_KEY] = AuditClient(str(settings.auth_service_url), session)


async def stop_audit_client(app: web.Application) -> None:
    session = app.get(_AUDIT_SESSION_KEY)
    if session is not None:
        await session.close()


async def start_script_runner(app: web.Application) -> None:
    runner = ScriptRunner(
        rabbitmq_url=str(settings.rabbitmq_url),
        service_name="experiment-service",
        max_concurrent=3,
    )
    app[_SCRIPT_RUNNER_KEY] = runner
    try:
        await runner.start()
    except Exception:
        import structlog
        structlog.get_logger(__name__).warning(
            "script_runner_start_failed",
            hint="RabbitMQ not available, script execution disabled",
        )


async def stop_script_runner(app: web.Application) -> None:
    runner = app.get(_SCRIPT_RUNNER_KEY)
    if runner is not None:
        await runner.stop()


def create_app() -> web.Application:
    app, cors = create_base_app(settings)
    app.middlewares.append(error_handling_middleware)
    app.middlewares.append(metrics_middleware("experiment-service"))
    app.middlewares.append(audit_middleware)

    app.add_routes(health_routes)
    add_openapi_spec(app, OPENAPI_PATH)
    setup_routes(app)
    app.router.add_get("/metrics", metrics_handler)

    # OpenTelemetry (auto-instruments aiohttp server; no-op when endpoint not set)
    setup_otel(app)

    app.on_startup.append(init_pool)
    app.on_startup.append(start_webhook_dispatcher)
    app.on_startup.append(start_background_worker)
    app.on_startup.append(start_audit_client)
    app.on_startup.append(start_script_runner)
    app.on_cleanup.append(stop_script_runner)
    app.on_cleanup.append(stop_audit_client)
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

