"""Pydantic DTOs for config-service API."""
from __future__ import annotations

from datetime import datetime
from typing import Any
from uuid import UUID

from pydantic import BaseModel, Field, field_validator

from config_service.domain.enums import ConfigType


class ConfigCreate(BaseModel):
    service_name: str = Field(min_length=1, max_length=128)
    project_id: str | None = None
    key: str = Field(min_length=1, max_length=128)
    config_type: ConfigType
    description: str | None = None
    value: dict[str, Any]
    metadata: dict[str, Any] = Field(default_factory=dict)
    is_critical: bool = False
    is_sensitive: bool = False
    change_reason: str | None = None


class ConfigPatch(BaseModel):
    version: int
    change_reason: str = Field(min_length=1)
    description: str | None = None
    value: dict[str, Any] | None = None
    metadata: dict[str, Any] | None = None
    is_active: bool | None = None
    is_critical: bool | None = None
    is_sensitive: bool | None = None


class ActivateDeactivateRequest(BaseModel):
    version: int
    change_reason: str = Field(min_length=1)


class RollbackRequest(BaseModel):
    version: int
    target_version: int
    change_reason: str = Field(min_length=1)

    @field_validator("target_version")
    @classmethod
    def target_must_differ(cls, v: int, info: Any) -> int:
        current = info.data.get("version")
        if current is not None and v == current:
            raise ValueError("target_version must differ from current version")
        return v


class SchemaUpdateRequest(BaseModel):
    schema_: dict[str, Any] = Field(alias="schema")

    model_config = {"populate_by_name": True}


# ─── Response DTOs ────────────────────────────────────────────────────────────


class ConfigResponse(BaseModel):
    id: UUID
    service_name: str
    project_id: str | None
    key: str
    config_type: ConfigType
    description: str | None
    value: dict[str, Any]
    metadata: dict[str, Any]
    is_active: bool
    is_critical: bool
    is_sensitive: bool
    version: int
    created_by: str
    updated_by: str
    created_at: datetime
    updated_at: datetime
    deleted_at: datetime | None = None

    model_config = {"from_attributes": True}


class ConfigHistoryResponse(BaseModel):
    id: UUID
    config_id: UUID
    version: int
    service_name: str
    key: str
    config_type: ConfigType
    value: dict[str, Any]
    metadata: dict[str, Any]
    is_active: bool
    changed_by: str
    change_reason: str | None
    source_ip: str | None
    user_agent: str | None
    correlation_id: str | None
    changed_at: datetime

    model_config = {"from_attributes": True}


class SchemaResponse(BaseModel):
    id: UUID
    config_type: ConfigType
    schema_: dict[str, Any] = Field(alias="schema")
    version: int
    is_active: bool
    created_by: str
    created_at: datetime

    model_config = {"from_attributes": True, "populate_by_name": True}


class BulkResponse(BaseModel):
    configs: dict[str, Any]


class ListResponse(BaseModel):
    items: list[ConfigResponse]
    next_cursor: str | None


class DryRunResponse(BaseModel):
    preview: ConfigResponse
    dry_run: bool = True
