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

# Active qos schema v2 (anyOf), mirrors migrations/003_update_qos_schema.sql.
from conftest import _QOS_SCHEMA as QOS_SCHEMA  # noqa: E402


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


def _make_qos_repo() -> MagicMock:
    repo = MagicMock()
    repo.get_active = AsyncMock(
        return_value=ConfigSchema(
            id=uuid.uuid4(),
            config_type=ConfigType.qos,
            schema=QOS_SCHEMA,
            version=2,
            is_active=True,
            created_by="system",
            created_at=datetime.now(tz=timezone.utc),
        )
    )
    return repo


@pytest.mark.asyncio
async def test_qos_auth_payload_valid():
    """auth_qos as written by auth-service / AuthQosForm (LOS-61)."""
    svc = ValidationService(_make_qos_repo())
    await svc.validate_strict(
        ConfigType.qos,
        {"access_token_ttl_sec": 900, "refresh_token_ttl_sec": 1209600},
    )


@pytest.mark.asyncio
async def test_qos_experiment_payload_valid():
    """experiment_qos as written by experiment-service / ExperimentQosForm (LOS-62)."""
    svc = ValidationService(_make_qos_repo())
    await svc.validate_strict(
        ConfigType.qos,
        {"rate_limit_max_requests": 100, "downstream_timeout_seconds": 5.0},
    )


@pytest.mark.asyncio
async def test_qos_telemetry_rate_limits_payload_valid():
    """rate_limits as written by TelemetryRateLimitsForm (LOS-63)."""
    svc = ValidationService(_make_qos_repo())
    await svc.validate_strict(
        ConfigType.qos,
        {
            "rest": {"max_requests": 600, "max_readings": 60000, "window_seconds": 60.0},
            "ws": {"max_messages": 600, "max_readings": 60000, "window_seconds": 1.0},
            "spool_flush_timeout_seconds": 5.0,
            "ws_max_message_bytes": 1048576,
        },
    )


@pytest.mark.asyncio
async def test_qos_telemetry_partial_payload_valid():
    """Partial updates are allowed — pollers treat missing fields as 'keep current'."""
    svc = ValidationService(_make_qos_repo())
    await svc.validate_strict(ConfigType.qos, {"spool_flush_timeout_seconds": 10.0})


@pytest.mark.asyncio
async def test_qos_unknown_keys_rejected():
    svc = ValidationService(_make_qos_repo())
    with pytest.raises(ConfigValidationError):
        await svc.validate_strict(ConfigType.qos, {"unknown_knob": 1})


@pytest.mark.asyncio
async def test_qos_wrong_type_rejected():
    svc = ValidationService(_make_qos_repo())
    with pytest.raises(ConfigValidationError):
        await svc.validate_strict(ConfigType.qos, {"access_token_ttl_sec": "900"})


@pytest.mark.asyncio
async def test_qos_empty_object_rejected():
    svc = ValidationService(_make_qos_repo())
    with pytest.raises(ConfigValidationError):
        await svc.validate_strict(ConfigType.qos, {})


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
