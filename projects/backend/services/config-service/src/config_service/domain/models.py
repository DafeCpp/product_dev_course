"""Domain dataclasses — plain Python objects, no framework deps."""
from __future__ import annotations

import json
from dataclasses import dataclass, field
from datetime import datetime
from typing import Any
from uuid import UUID

from config_service.domain.enums import ConfigType


def _decode_jsonb(v: Any) -> Any:
    return json.loads(v) if isinstance(v, str) else v


@dataclass
class Config:
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
    deleted_at: datetime | None

    @classmethod
    def from_row(cls, row: dict[str, Any]) -> Config:
        return cls(
            id=row["id"],
            service_name=row["service_name"],
            project_id=row["project_id"],
            key=row["key"],
            config_type=ConfigType(row["config_type"]),
            description=row["description"],
            value=_decode_jsonb(row["value"]),
            metadata=_decode_jsonb(row["metadata"]) or {},
            is_active=row["is_active"],
            is_critical=row["is_critical"],
            is_sensitive=row["is_sensitive"],
            version=row["version"],
            created_by=row["created_by"],
            updated_by=row["updated_by"],
            created_at=row["created_at"],
            updated_at=row["updated_at"],
            deleted_at=row.get("deleted_at"),
        )


@dataclass
class ConfigHistory:
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

    @classmethod
    def from_row(cls, row: dict[str, Any]) -> ConfigHistory:
        return cls(
            id=row["id"],
            config_id=row["config_id"],
            version=row["version"],
            service_name=row["service_name"],
            key=row["key"],
            config_type=ConfigType(row["config_type"]),
            value=_decode_jsonb(row["value"]),
            metadata=_decode_jsonb(row["metadata"]) or {},
            is_active=row["is_active"],
            changed_by=row["changed_by"],
            change_reason=row.get("change_reason"),
            source_ip=row.get("source_ip"),
            user_agent=row.get("user_agent"),
            correlation_id=row.get("correlation_id"),
            changed_at=row["changed_at"],
        )


@dataclass
class ConfigSchema:
    id: UUID
    config_type: ConfigType
    schema: dict[str, Any]
    version: int
    is_active: bool
    created_by: str
    created_at: datetime

    @classmethod
    def from_row(cls, row: dict[str, Any]) -> ConfigSchema:
        return cls(
            id=row["id"],
            config_type=ConfigType(row["config_type"]),
            schema=_decode_jsonb(row["schema"]),
            version=row["version"],
            is_active=row["is_active"],
            created_by=row["created_by"],
            created_at=row["created_at"],
        )


@dataclass
class IdempotencyRecord:
    id: UUID
    idempotency_key: str
    user_id: str
    request_path: str
    request_hash: str
    response_status: int
    response_body: dict[str, Any]
    expires_at: datetime
    created_at: datetime

    @classmethod
    def from_row(cls, row: dict[str, Any]) -> IdempotencyRecord:
        return cls(
            id=row["id"],
            idempotency_key=row["idempotency_key"],
            user_id=row["user_id"],
            request_path=row["request_path"],
            request_hash=row["request_hash"],
            response_status=row["response_status"],
            response_body=_decode_jsonb(row["response_body"]),
            expires_at=row["expires_at"],
            created_at=row["created_at"],
        )
