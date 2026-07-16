from datetime import datetime, timezone
from uuid import uuid4

import pytest
from aiohttp import web

from backend_common.api.parsers import parse_bool, parse_int, parse_rfc3339
from telemetry_ingest_service.services.serializers import (
    serialize_aggregated_record,
    serialize_telemetry_record,
)


def test_shared_parsers_keep_telemetry_query_semantics() -> None:
    assert parse_int(None, default=12) == 12
    assert parse_int("42", default=0) == 42
    assert parse_bool("yes", default=False) is True
    assert parse_bool("off", default=True) is False
    assert parse_rfc3339("2026-01-01T12:00:00Z") == datetime(2026, 1, 1, 12, tzinfo=timezone.utc)


@pytest.mark.parametrize("value", ["nope", ""])
def test_parse_bool_rejects_invalid_values(value: str) -> None:
    with pytest.raises(web.HTTPBadRequest):
        parse_bool(value, default=True)


def test_telemetry_serializer_normalizes_json_and_timestamp() -> None:
    project_id, sensor_id = uuid4(), uuid4()
    payload = serialize_telemetry_record({
        "id": 7,
        "project_id": project_id,
        "sensor_id": sensor_id,
        "timestamp": datetime(2026, 1, 1, 12, tzinfo=timezone.utc),
        "raw_value": 2.5,
        "physical_value": None,
        "run_id": None,
        "capture_session_id": None,
        "meta": '{"source":"test"}',
    })
    assert payload == {
        "id": 7, "project_id": str(project_id), "sensor_id": str(sensor_id),
        "timestamp": "2026-01-01T12:00:00Z", "raw_value": 2.5, "physical_value": None,
        "run_id": None, "capture_session_id": None, "meta": {"source": "test"},
    }


def test_aggregated_serializer_normalizes_bucket() -> None:
    payload = serialize_aggregated_record({
        "bucket": datetime(2026, 1, 1, 12, tzinfo=timezone.utc), "sensor_id": None,
        "signal": "temperature", "capture_session_id": None, "sample_count": 3,
        "avg_raw": 1.0, "min_raw": 0.0, "max_raw": 2.0,
        "avg_physical": 1.5, "min_physical": 0.5, "max_physical": 2.5,
    })
    assert payload["bucket"] == "2026-01-01T12:00:00Z"
    assert payload["sample_count"] == 3
