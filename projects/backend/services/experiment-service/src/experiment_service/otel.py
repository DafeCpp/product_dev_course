"""OpenTelemetry instrumentation for experiment-service.

Activated only when ``otel_exporter_endpoint`` is set in settings.
Provides:
  - TracerProvider with OTLP HTTP exporter
  - aiohttp server auto-instrumentation (creates spans per request)
  - Helper ``get_tracer`` for manual spans in service/repository code.
"""
from __future__ import annotations

import structlog
from aiohttp import web

from opentelemetry import trace
from opentelemetry.sdk.resources import Resource, SERVICE_NAME
from opentelemetry.sdk.trace import TracerProvider
from opentelemetry.sdk.trace.export import BatchSpanProcessor
from opentelemetry.exporter.otlp.proto.http.trace_exporter import OTLPSpanExporter
from opentelemetry.instrumentation.aiohttp_server import AioHttpServerInstrumentor

from experiment_service.settings import settings

logger = structlog.get_logger(__name__)

_provider: TracerProvider | None = None


def setup_otel(app: web.Application) -> None:
    """Initialise OpenTelemetry tracing if ``otel_exporter_endpoint`` is configured."""
    global _provider

    endpoint = settings.otel_exporter_endpoint
    if not endpoint:
        logger.info("otel_exporter_endpoint not set — OpenTelemetry tracing disabled")
        return

    resource = Resource.create({SERVICE_NAME: settings.app_name})
    _provider = TracerProvider(resource=resource)

    exporter = OTLPSpanExporter(endpoint=f"{endpoint}/v1/traces")
    _provider.add_span_processor(BatchSpanProcessor(exporter))

    trace.set_tracer_provider(_provider)

    # Auto-instrument aiohttp server (adds a span per request)
    AioHttpServerInstrumentor().instrument(server=app)

    logger.info(
        "OpenTelemetry tracing enabled",
        endpoint=str(endpoint),
        service=settings.app_name,
    )


async def shutdown_otel(_app: web.Application) -> None:
    """Flush pending spans on application shutdown."""
    global _provider
    if _provider is not None:
        _provider.shutdown()
        logger.info("OpenTelemetry tracer provider shut down")
        _provider = None


def get_tracer(name: str = __name__) -> trace.Tracer:
    """Return a tracer for manual instrumentation.

    Safe to call even when OTel is disabled — returns a no-op tracer.
    """
    return trace.get_tracer(name)
