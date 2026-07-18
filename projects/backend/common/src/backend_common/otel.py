"""Shared OpenTelemetry instrumentation for aiohttp services."""
from __future__ import annotations

from typing import Any

import structlog
from aiohttp import web
from opentelemetry import trace
from opentelemetry.exporter.otlp.proto.http.trace_exporter import OTLPSpanExporter
from opentelemetry.instrumentation.aiohttp_server import AioHttpServerInstrumentor
from opentelemetry.sdk.resources import SERVICE_NAME, Resource
from opentelemetry.sdk.trace import TracerProvider
from opentelemetry.sdk.trace.export import BatchSpanProcessor

logger = structlog.get_logger(__name__)

_provider: TracerProvider | None = None


def setup_otel(
    app: web.Application,
    *,
    service_name: str,
    exporter_endpoint: Any | None,
) -> None:
    """Enable OTLP tracing when an exporter endpoint is configured."""
    global _provider

    if not exporter_endpoint:
        logger.info("otel_exporter_endpoint_not_set", service=service_name)
        return

    resource = Resource.create({SERVICE_NAME: service_name})
    _provider = TracerProvider(resource=resource)
    exporter = OTLPSpanExporter(endpoint=f"{exporter_endpoint}/v1/traces")
    _provider.add_span_processor(BatchSpanProcessor(exporter))
    trace.set_tracer_provider(_provider)
    AioHttpServerInstrumentor().instrument(server=app)

    logger.info("otel_tracing_enabled", endpoint=str(exporter_endpoint), service=service_name)


async def shutdown_otel(_app: web.Application) -> None:
    """Flush pending spans during aiohttp application cleanup."""
    global _provider

    if _provider is not None:
        _provider.shutdown()
        logger.info("otel_tracer_provider_shutdown")
        _provider = None


def get_tracer(name: str = __name__) -> trace.Tracer:
    """Return a tracer; it is a no-op when OTel has not been configured."""
    return trace.get_tracer(name)
