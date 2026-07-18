from datetime import datetime, timezone
from uuid import uuid4

import pytest
from aiohttp import web
from aiohttp.test_utils import make_mocked_request

from backend_common.api.parsers import (
    pagination_params,
    parse_bool,
    parse_datetime,
    parse_int,
    parse_optional_uuid,
    parse_rfc3339,
    parse_uuid,
)


def test_uuid_parsers_accept_values_and_reject_invalid_input() -> None:
    value = uuid4()
    assert parse_uuid(str(value), "id") == value
    assert parse_optional_uuid(None) is None
    with pytest.raises(web.HTTPBadRequest) as error:
        parse_uuid("not-a-uuid", "id")
    assert error.value.text == "Invalid id"


def test_datetime_parsers_normalize_and_validate() -> None:
    assert parse_datetime(None, "created") is None
    assert parse_datetime("2026-01-01T12:00:00", "created").tzinfo == timezone.utc
    assert parse_rfc3339("2026-01-01T12:00:00Z") == datetime(2026, 1, 1, 12, tzinfo=timezone.utc)
    with pytest.raises(web.HTTPBadRequest):
        parse_datetime("yesterday", "created")
    with pytest.raises(web.HTTPBadRequest):
        parse_rfc3339("nope")


def test_query_parsers_cover_defaults_validation_and_bounds() -> None:
    assert parse_int(None, default=4) == 4
    assert parse_bool("YES", default=False) is True
    assert parse_bool("off", default=True) is False
    with pytest.raises(web.HTTPBadRequest):
        parse_int("one", default=4)
    with pytest.raises(web.HTTPBadRequest):
        parse_bool("sometimes", default=False)

    request = make_mocked_request("GET", "/?limit=500&offset=-2")
    assert pagination_params(request, max_limit=100) == (100, 0)
    with pytest.raises(web.HTTPBadRequest):
        pagination_params(make_mocked_request("GET", "/?limit=one"))
