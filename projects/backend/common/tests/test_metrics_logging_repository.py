import logging
from unittest.mock import AsyncMock, MagicMock

import pytest
from aiohttp import web
from aiohttp.test_utils import make_mocked_request

from backend_common.logging_config import SingleLineFormatter, _sanitize_string, replace_newlines_processor
from backend_common.metrics import metrics_handler, metrics_middleware
from backend_common.repositories.base import BaseRepository


def test_logging_processors_escape_control_characters() -> None:
    assert _sanitize_string("a\\b\nc\r\td") == "a\\\\b\\nc\\r\\td"
    event = replace_newlines_processor(None, "info", {"text": "a\nb", "items": ["c\td"], "nested": {"v": "x\ry"}})
    assert event == {"text": "a\\nb", "items": ["c\\td"], "nested": {"v": "x\\ry"}}
    record = logging.LogRecord("test", logging.INFO, __file__, 1, "first\nsecond", (), None)
    assert SingleLineFormatter("%(message)s").format(record) == "first\\nsecond"


@pytest.mark.asyncio
async def test_metrics_middleware_records_success_and_http_error() -> None:
    middleware = metrics_middleware("common-tests")
    request = make_mocked_request("GET", "/items")
    response = await middleware(request, AsyncMock(return_value=web.Response(status=201)))
    assert response.status == 201

    async def raises_http_error(_request):
        raise web.HTTPNotFound()

    with pytest.raises(web.HTTPNotFound):
        await middleware(request, raises_http_error)
    metrics_response = await metrics_handler(request)
    assert metrics_response.status == 200
    assert b"http_requests_total" in metrics_response.body


@pytest.mark.asyncio
async def test_base_repository_delegates_to_acquired_connection() -> None:
    connection = AsyncMock()
    connection.fetchrow.return_value = {"id": 1}
    connection.fetch.return_value = [{"id": 1}]
    connection.execute.return_value = "UPDATE 1"
    pool = MagicMock()
    pool.acquire.return_value.__aenter__ = AsyncMock(return_value=connection)
    pool.acquire.return_value.__aexit__ = AsyncMock(return_value=None)
    repository = BaseRepository(pool)

    assert await repository._fetchrow("SELECT 1", 1) == {"id": 1}
    assert list(await repository._fetch("SELECT 1")) == [{"id": 1}]
    assert await repository._execute("UPDATE table SET value = $1", "value") == "UPDATE 1"
