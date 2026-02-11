"""Shared aiohttp application helpers."""
from __future__ import annotations

from pathlib import Path
from typing import Any, Literal, Protocol

from aiohttp import web
from aiohttp_cors import CorsConfig, ResourceOptions, setup as cors_setup

from backend_common.middleware.trace import create_trace_middleware

# Allowed CORS headers (explicitly listed for security instead of wildcard "*").
# aiohttp_cors expects a sequence of strings (or "*"), NOT a comma-separated string.
_ALLOWED_HEADERS = (
    "Accept",
    "Accept-Language",
    "Content-Language",
    "Content-Type",
    "Authorization",
    "X-Trace-Id",
    "X-Request-Id",
    "X-Project-Id",
    "X-Project-Role",
    "X-User-Id",
    "X-Idempotency-Key",
)

_ALLOWED_METHODS = (
    "GET",
    "HEAD",
    "POST",
    "PUT",
    "PATCH",
    "DELETE",
    "OPTIONS",
)

_EXPOSED_HEADERS = (
    "X-Trace-Id",
    "X-Request-Id",
)


class SettingsProtocol(Protocol):
    """Protocol for settings objects used by the app helpers."""

    app_name: str
    env: Literal["development", "staging", "production"]
    cors_allowed_origins: list[str]


def create_base_app(settings: SettingsProtocol) -> tuple[web.Application, CorsConfig]:
    """Create a base aiohttp app with tracing middleware and CORS configured."""
    app = web.Application()

    trace_middleware = create_trace_middleware(settings.app_name)
    app.middlewares.append(trace_middleware)

    cors = cors_setup(
        app,
        defaults={
            origin: ResourceOptions(
                allow_credentials=True,
                expose_headers=_EXPOSED_HEADERS,
                allow_headers=_ALLOWED_HEADERS,
                allow_methods=_ALLOWED_METHODS,
            )
            for origin in settings.cors_allowed_origins
        },
    )

    return app, cors


def add_healthcheck(app: web.Application, settings: SettingsProtocol) -> None:
    """Register a standard health check endpoint."""

    async def healthcheck(_request: web.Request) -> web.Response:
        return web.json_response({"status": "ok", "service": settings.app_name, "env": settings.env})

    app.router.add_get("/health", healthcheck)


def add_openapi_spec(app: web.Application, openapi_path: Path) -> None:
    """Register an endpoint that serves the OpenAPI spec."""

    async def openapi_spec(_request: web.Request) -> web.StreamResponse:
        return web.FileResponse(openapi_path, headers={"Content-Type": "application/yaml"})

    app.router.add_get("/openapi.yaml", openapi_spec)


def add_cors_to_routes(app: web.Application, cors: CorsConfig) -> None:
    """Apply CORS configuration to all routes in the app."""
    for route in list(app.router.routes()):
        cors.add(route)


async def read_json(request: web.Request) -> dict[str, Any]:
    """Parse JSON body from request, raising HTTPBadRequest on invalid input.

    This is a shared utility to avoid duplicating JSON parsing logic
    across services.
    """
    try:
        data = await request.json()
    except Exception as exc:
        raise web.HTTPBadRequest(text="Invalid JSON payload") from exc
    if not isinstance(data, dict):
        raise web.HTTPBadRequest(text="JSON body must be an object")
    return data


def extract_bearer_token(request: web.Request) -> str:
    """Extract Bearer token from Authorization header.

    Raises web.HTTPUnauthorized if the header is missing or malformed.
    """
    auth_header = request.headers.get("Authorization")
    if not auth_header or not auth_header.startswith("Bearer "):
        raise web.HTTPUnauthorized(reason="Authorization token is required")
    token = auth_header[len("Bearer "):].strip()
    if not token:
        raise web.HTTPUnauthorized(reason="Authorization token is required")
    return token
