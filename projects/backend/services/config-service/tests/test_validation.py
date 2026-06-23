"""Unit tests for ValidationService (no DB required — mocked schema_repo)."""
from __future__ import annotations

from unittest.mock import AsyncMock, MagicMock

import pytest

from config_service.core.exceptions import ConfigValidationError
from config_service.domain.enums import ConfigType
from config_service.domain.models import ConfigSchema
from config_service.services.validation_service import ValidationService
import uuid
from datetime import datetime, timezone


def _make_schema_repo(schema_dict: dict) -> MagicMock:
    repo = MagicMock()
    schema_obj = ConfigSchema(
        id=uuid.uuid4(),
        config_type=ConfigType.feature_flag,
        schema=schema_dict,
        version=1,
        is_active=True,
        created_by="system",
        created_at=datetime.now(tz=timezone.utc),
    )
    repo.get_active = AsyncMock(return_value=schema_obj)
    return repo


FEATURE_FLAG_SCHEMA = {
    "$schema": "https://json-schema.org/draft/2020-12/schema",
    "type": "object",
    "required": ["enabled"],
    "properties": {"enabled": {"type": "boolean"}},
    "additionalProperties": False,
}

QOS_SCHEMA = {
    "$schema": "https://json-schema.org/draft/2020-12/schema",
    "type": "object",
    "required": ["__default__"],
    "additionalProperties": {"$ref": "#/$defs/qosSettings"},
    "properties": {"__default__": {"$ref": "#/$defs/qosSettings"}},
    "$defs": {
        "qosSettings": {
            "type": "object",
            "required": ["timeout_ms", "retries"],
            "properties": {
                "timeout_ms": {"type": "integer", "minimum": 1, "maximum": 600000},
                "retries": {"type": "integer", "minimum": 0, "maximum": 10},
            },
            "additionalProperties": False,
        }
    },
}


@pytest.mark.asyncio
async def test_feature_flag_valid():
    repo = _make_schema_repo(FEATURE_FLAG_SCHEMA)
    svc = ValidationService(repo)
    # Should not raise
    await svc.validate_strict(ConfigType.feature_flag, {"enabled": True})


@pytest.mark.asyncio
async def test_feature_flag_missing_enabled():
    repo = _make_schema_repo(FEATURE_FLAG_SCHEMA)
    svc = ValidationService(repo)
    with pytest.raises(ConfigValidationError):
        await svc.validate_strict(ConfigType.feature_flag, {})


@pytest.mark.asyncio
async def test_feature_flag_wrong_type():
    repo = _make_schema_repo(FEATURE_FLAG_SCHEMA)
    svc = ValidationService(repo)
    with pytest.raises(ConfigValidationError):
        await svc.validate_strict(ConfigType.feature_flag, {"enabled": "yes"})


@pytest.mark.asyncio
async def test_feature_flag_additional_properties_not_allowed():
    repo = _make_schema_repo(FEATURE_FLAG_SCHEMA)
    svc = ValidationService(repo)
    with pytest.raises(ConfigValidationError):
        await svc.validate_strict(ConfigType.feature_flag, {"enabled": True, "extra": 1})


@pytest.mark.asyncio
async def test_qos_valid():
    repo = _make_schema_repo(QOS_SCHEMA)
    repo.get_active = AsyncMock(
        return_value=ConfigSchema(
            id=uuid.uuid4(),
            config_type=ConfigType.qos,
            schema=QOS_SCHEMA,
            version=1,
            is_active=True,
            created_by="system",
            created_at=datetime.now(tz=timezone.utc),
        )
    )
    svc = ValidationService(repo)
    await svc.validate_strict(
        ConfigType.qos,
        {
            "__default__": {"timeout_ms": 150, "retries": 2},
            "/v1/verify": {"timeout_ms": 10000, "retries": 5},
        },
    )


@pytest.mark.asyncio
async def test_qos_missing_default():
    repo = _make_schema_repo(QOS_SCHEMA)
    repo.get_active = AsyncMock(
        return_value=ConfigSchema(
            id=uuid.uuid4(),
            config_type=ConfigType.qos,
            schema=QOS_SCHEMA,
            version=1,
            is_active=True,
            created_by="system",
            created_at=datetime.now(tz=timezone.utc),
        )
    )
    svc = ValidationService(repo)
    with pytest.raises(ConfigValidationError):
        await svc.validate_strict(ConfigType.qos, {"/v1/verify": {"timeout_ms": 100, "retries": 1}})


@pytest.mark.asyncio
async def test_paranoid_logs_metric_but_does_not_raise(monkeypatch):
    repo = _make_schema_repo(FEATURE_FLAG_SCHEMA)
    svc = ValidationService(repo)
    # Should not raise even with invalid value
    await svc.validate_paranoid(ConfigType.feature_flag, {"enabled": "not-a-bool"}, "cfg-123")


@pytest.mark.asyncio
async def test_cache_invalidation():
    repo = _make_schema_repo(FEATURE_FLAG_SCHEMA)
    svc = ValidationService(repo)
    # First call populates cache
    await svc.validate_strict(ConfigType.feature_flag, {"enabled": True})
    assert repo.get_active.call_count == 1
    # Second call uses cache
    await svc.validate_strict(ConfigType.feature_flag, {"enabled": False})
    assert repo.get_active.call_count == 1
    # After invalidation, re-fetches
    svc.invalidate_cache(ConfigType.feature_flag)
    await svc.validate_strict(ConfigType.feature_flag, {"enabled": True})
    assert repo.get_active.call_count == 2
