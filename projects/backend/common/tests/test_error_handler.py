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
)
from backend_common.middleware.error_handler import error_handling_middleware


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
