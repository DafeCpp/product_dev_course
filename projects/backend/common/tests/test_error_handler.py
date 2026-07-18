"""Tests for common service-error HTTP mapping."""
from __future__ import annotations

from unittest.mock import AsyncMock, MagicMock

import pytest
from aiohttp import web

from backend_common.core.exceptions import (
    ConflictError,
    ForbiddenError,
    NotFoundError,
    UnauthorizedError,
    ValidationError,
    ServiceError,
)
from backend_common.middleware.error_handler import error_handling_middleware, register_error_mappings


@pytest.mark.asyncio
@pytest.mark.parametrize(
    ("error", "status"),
    [
        (NotFoundError("missing"), 404),
        (UnauthorizedError("unauthorized"), 401),
        (ForbiddenError("forbidden"), 403),
        (ConflictError("conflict"), 409),
        (ValidationError("invalid"), 400),
    ],
)
async def test_maps_common_service_errors_to_statuses(error: Exception, status: int) -> None:
    handler = AsyncMock(side_effect=error)

    response = await error_handling_middleware(MagicMock(spec=web.Request), handler)

    assert response.status == status


@pytest.mark.asyncio
async def test_preserves_domain_specific_status_code() -> None:
    class UnprocessableValidationError(ValidationError):
        status_code = 422

    handler = AsyncMock(side_effect=UnprocessableValidationError("invalid"))

    response = await error_handling_middleware(MagicMock(spec=web.Request), handler)

    assert response.status == 422


@pytest.mark.asyncio
async def test_preserves_aiohttp_error_and_maps_registered_error(monkeypatch: pytest.MonkeyPatch) -> None:
    request = MagicMock(spec=web.Request)
    aiohttp_handler = AsyncMock(side_effect=web.HTTPTooManyRequests())

    with pytest.raises(web.HTTPTooManyRequests):
        await error_handling_middleware(request, aiohttp_handler)

    monkeypatch.setattr("backend_common.middleware.error_handler._extra_mappings", {})
    register_error_mappings({KeyError: 418})
    response = await error_handling_middleware(request, AsyncMock(side_effect=KeyError("missing")))

    assert response.status == 418
    assert response.text == '{"error": "\'missing\'"}'


@pytest.mark.asyncio
async def test_hides_unexpected_error_details(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("backend_common.middleware.error_handler._extra_mappings", {})

    response = await error_handling_middleware(
        MagicMock(spec=web.Request), AsyncMock(side_effect=RuntimeError("sensitive detail"))
    )

    assert response.status == 500
    assert response.text == '{"error": "Internal server error"}'


@pytest.mark.asyncio
async def test_uses_exception_class_name_for_empty_server_error() -> None:
    class EmptyServerError(ServiceError):
        status_code = 503

    response = await error_handling_middleware(
        MagicMock(spec=web.Request), AsyncMock(side_effect=EmptyServerError())
    )

    assert response.status == 503
    assert response.text == '{"error": "EmptyServerError"}'
