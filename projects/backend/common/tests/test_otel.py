"""Tests for shared OpenTelemetry setup."""
from __future__ import annotations

from unittest.mock import MagicMock, patch

import pytest
from aiohttp import web

import backend_common.otel as otel


@pytest.fixture(autouse=True)
def reset_provider() -> None:
    otel._provider = None
    yield
    otel._provider = None


def test_setup_is_noop_without_exporter_endpoint() -> None:
    with patch("backend_common.otel.TracerProvider") as provider:
        otel.setup_otel(web.Application(), service_name="test-service", exporter_endpoint=None)

    provider.assert_not_called()


def test_setup_configures_provider_exporter_and_aiohttp_instrumentation() -> None:
    provider = MagicMock()
    exporter = MagicMock()
    instrumentor = MagicMock()
    app = web.Application()

    with (
        patch("backend_common.otel.Resource.create") as resource_create,
        patch("backend_common.otel.TracerProvider", return_value=provider) as provider_class,
        patch("backend_common.otel.OTLPSpanExporter", return_value=exporter) as exporter_class,
        patch("backend_common.otel.BatchSpanProcessor") as processor_class,
        patch("backend_common.otel.trace.set_tracer_provider") as set_provider,
        patch("backend_common.otel.AioHttpServerInstrumentor", return_value=instrumentor),
    ):
        otel.setup_otel(
            app,
            service_name="test-service",
            exporter_endpoint="http://otel:4318",
        )

    resource_create.assert_called_once_with({otel.SERVICE_NAME: "test-service"})
    provider_class.assert_called_once()
    exporter_class.assert_called_once_with(endpoint="http://otel:4318/v1/traces")
    provider.add_span_processor.assert_called_once_with(processor_class.return_value)
    set_provider.assert_called_once_with(provider)
    instrumentor.instrument.assert_called_once_with(server=app)


@pytest.mark.asyncio
async def test_shutdown_flushes_configured_provider() -> None:
    provider = MagicMock()
    otel._provider = provider

    await otel.shutdown_otel(web.Application())

    provider.shutdown.assert_called_once()
    assert otel._provider is None
