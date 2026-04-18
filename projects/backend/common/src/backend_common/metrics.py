"""Shared Prometheus metrics middleware and handler for aiohttp services."""
from __future__ import annotations

import time
from collections.abc import Awaitable, Callable
from typing import Any, TYPE_CHECKING

from aiohttp import web
from prometheus_client import (
    CONTENT_TYPE_LATEST,
    Counter,
    Gauge,
    Histogram,
    generate_latest,
)

if TYPE_CHECKING:
    pass

# ---------------------------------------------------------------------------
# Global metric descriptors (shared across all services via label 'service')
# ---------------------------------------------------------------------------

REQUEST_COUNT: Counter = Counter(
    "http_requests_total",
    "Total HTTP requests",
    ["service", "method", "endpoint", "status"],
)

REQUEST_LATENCY: Histogram = Histogram(
    "http_request_duration_seconds",
    "HTTP request latency in seconds",
    ["service", "method", "endpoint"],
)

REQUESTS_IN_PROGRESS: Gauge = Gauge(
    "http_requests_in_progress",
    "Number of HTTP requests currently being processed",
    ["service"],
)


# ---------------------------------------------------------------------------
# Middleware factory
# ---------------------------------------------------------------------------

Handler = Callable[[web.Request], Awaitable[web.StreamResponse]]


def metrics_middleware(service_name: str) -> Any:
    """Return an aiohttp middleware that records Prometheus HTTP metrics.

    Usage::

        app.middlewares.append(metrics_middleware("my-service"))
    """

    @web.middleware
    async def middleware(request: web.Request, handler: Handler) -> web.StreamResponse:
        # Skip recording metrics for the /metrics endpoint itself to avoid noise.
        if request.path == "/metrics":
            return await handler(request)

        REQUESTS_IN_PROGRESS.labels(service=service_name).inc()
        start = time.monotonic()
        status_code = 500
        try:
            response = await handler(request)
            status_code = response.status
            return response
        except web.HTTPException as exc:
            status_code = exc.status_code
            raise
        finally:
            duration = time.monotonic() - start
            REQUESTS_IN_PROGRESS.labels(service=service_name).dec()
            REQUEST_COUNT.labels(
                service=service_name,
                method=request.method,
                endpoint=request.path,
                status=str(status_code),
            ).inc()
            REQUEST_LATENCY.labels(
                service=service_name,
                method=request.method,
                endpoint=request.path,
            ).observe(duration)

    return middleware  # type: ignore[return-value]


# ---------------------------------------------------------------------------
# /metrics handler
# ---------------------------------------------------------------------------


async def metrics_handler(request: web.Request) -> web.Response:
    """Serve Prometheus metrics in text exposition format.

    Register this handler **without** authentication::

        app.router.add_get("/metrics", metrics_handler)
    """
    output = generate_latest()
    # aiohttp does not allow charset inside content_type kwarg.
    # Pass the full Prometheus content-type string via the headers dict instead.
    return web.Response(
        body=output,
        headers={"Content-Type": CONTENT_TYPE_LATEST},
    )
