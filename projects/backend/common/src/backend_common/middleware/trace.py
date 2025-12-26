"""Middleware for trace_id and request_id logging."""
from __future__ import annotations

import structlog
from aiohttp import web
from uuid import UUID, uuid4


TRACE_ID_HEADER = "X-Trace-Id"
REQUEST_ID_HEADER = "X-Request-Id"

logger = structlog.get_logger(__name__)


def is_valid_uuid(value: str) -> bool:
    """Check if string is a valid UUID."""
    try:
        UUID(value)
        return True
    except (ValueError, AttributeError):
        return False


def create_trace_middleware(service_name: str):
    """Create trace middleware with specified service name."""
    @web.middleware
    async def trace_middleware(request: web.Request, handler):
        """Middleware to extract and log trace_id and request_id."""
        # Extract or generate trace_id
        trace_id = request.headers.get(TRACE_ID_HEADER)
        if not trace_id or not is_valid_uuid(trace_id):
            trace_id = str(uuid4())

        # Extract or generate request_id
        request_id = request.headers.get(REQUEST_ID_HEADER)
        if not request_id or not is_valid_uuid(request_id):
            request_id = str(uuid4())

        # Store in request for use in handlers
        request["trace_id"] = trace_id
        request["request_id"] = request_id

        # Configure structlog context for this request
        structlog.contextvars.clear_contextvars()
        structlog.contextvars.bind_contextvars(
            trace_id=trace_id,
            request_id=request_id,
            service=service_name,
            method=request.method,
            path=request.path,
        )

        # Log incoming request
        logger.info(
            "Incoming request",
            method=request.method,
            path=request.path,
            query_string=str(request.query_string),
        )

        try:
            response = await handler(request)

            # Log successful response
            logger.info(
                "Request completed",
                method=request.method,
                path=request.path,
                status_code=response.status,
            )

            # Add trace_id and request_id to response headers
            response.headers[TRACE_ID_HEADER] = trace_id
            response.headers[REQUEST_ID_HEADER] = request_id

            return response
        except Exception as e:
            # Log error with trace context
            logger.error(
                "Request failed",
                method=request.method,
                path=request.path,
                error=str(e),
                error_type=type(e).__name__,
                exc_info=True,
            )
            raise
        finally:
            # Clear context vars after request
            structlog.contextvars.clear_contextvars()

    return trace_middleware

