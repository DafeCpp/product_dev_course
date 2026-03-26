"""Middleware that maps domain exceptions to JSON HTTP error responses.

Usage::

    from backend_common.middleware.error_handler import error_handling_middleware

    app.middlewares.append(error_handling_middleware)

The middleware catches any ``ServiceError`` (or its subclasses) raised by
request handlers and returns a ``web.json_response`` with the appropriate
HTTP status code.  Uncaught non-ServiceError exceptions are logged and
returned as 500.

Services may register *additional* exception mappings via
``register_error_mappings`` for service-specific exception classes that
don't inherit from ``ServiceError``.
"""
from __future__ import annotations

import structlog
from aiohttp import web
from aiohttp.web import middleware

from backend_common.core.exceptions import ServiceError

logger = structlog.get_logger(__name__)

# Extra (service-specific) exception → status_code mappings.
_extra_mappings: dict[type[Exception], int] = {}


def register_error_mappings(mappings: dict[type[Exception], int]) -> None:
    """Register additional exception-class → HTTP-status-code mappings.

    Call this at service startup (before requests are served) so that the
    middleware knows how to handle service-specific exceptions that don't
    inherit from ``ServiceError``.
    """
    _extra_mappings.update(mappings)


@middleware
async def error_handling_middleware(
    request: web.Request,
    handler: web.RequestHandler,
) -> web.StreamResponse:
    try:
        return await handler(request)
    except web.HTTPException:
        # Let aiohttp's own HTTP exceptions pass through unchanged.
        raise
    except ServiceError as exc:
        status = exc.status_code
        message = str(exc) or type(exc).__name__
        if status >= 500:
            logger.exception("Unhandled service error", status=status)
        return web.json_response({"error": message}, status=status)
    except Exception as exc:
        # Check extra mappings registered by the service.
        for exc_cls, status in _extra_mappings.items():
            if isinstance(exc, exc_cls):
                message = str(exc) or type(exc).__name__
                return web.json_response({"error": message}, status=status)

        logger.exception("Unhandled exception")
        return web.json_response(
            {"error": "Internal server error"},
            status=500,
        )
