"""Unit tests for domain DTOs and models."""
from __future__ import annotations

import pytest
from pydantic import ValidationError

from config_service.domain.dto import (
    ConfigCreate,
    ConfigPatch,
    RollbackRequest,
    SchemaUpdateRequest,
)
from config_service.domain.enums import ConfigType


def test_config_create_valid():
    dto = ConfigCreate(
        service_name="my-service",
        key="write_path_enabled",
        config_type=ConfigType.feature_flag,
        value={"enabled": True},
    )
    assert dto.service_name == "my-service"
    assert dto.config_type == ConfigType.feature_flag
    assert dto.metadata == {}
    assert dto.is_critical is False
    assert dto.is_sensitive is False


def test_config_create_missing_required_fields():
    with pytest.raises(ValidationError):
        ConfigCreate(key="k", config_type=ConfigType.feature_flag, value={})  # type: ignore


def test_config_create_empty_service_name():
    with pytest.raises(ValidationError):
        ConfigCreate(
            service_name="",
            key="k",
            config_type=ConfigType.feature_flag,
            value={},
        )


def test_config_patch_requires_change_reason():
    with pytest.raises(ValidationError):
        ConfigPatch(version=1, change_reason="")


def test_config_patch_valid():
    dto = ConfigPatch(
        version=1,
        change_reason="INC-1234",
        value={"enabled": False},
    )
    assert dto.version == 1
    assert dto.change_reason == "INC-1234"
    assert dto.value == {"enabled": False}
    assert dto.is_active is None


def test_rollback_request_target_must_differ():
    with pytest.raises(ValidationError):
        RollbackRequest(version=2, target_version=2, change_reason="test")


def test_rollback_request_valid():
    dto = RollbackRequest(version=3, target_version=1, change_reason="restore")
    assert dto.target_version == 1


def test_schema_update_request_alias():
    dto = SchemaUpdateRequest.model_validate({"schema": {"type": "object"}})
    assert dto.schema_ == {"type": "object"}


def test_config_type_enum_values():
    assert ConfigType("feature_flag") == ConfigType.feature_flag
    assert ConfigType("qos") == ConfigType.qos
    with pytest.raises(ValueError):
        ConfigType("unknown")
