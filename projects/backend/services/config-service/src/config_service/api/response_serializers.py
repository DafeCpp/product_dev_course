"""Centralized serialization and sensitive-value redaction for user APIs."""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any

from config_service.domain.dto import ConfigResponse
from config_service.services.dependencies import UserContext

REDACTED_VALUE = "***"


def can_read_sensitive(user: UserContext) -> bool:
    return user.is_superadmin or "configs.sensitive.read" in user.system_permissions


def serialize_config_full(config: object) -> dict[str, Any]:
    """Return the canonical, unredacted representation used by trusted storage."""
    return ConfigResponse.model_validate(config, from_attributes=True).model_dump(mode="json")


def serialize_config(config: object, user: UserContext) -> dict[str, Any]:
    result = serialize_config_full(config)
    if result["is_sensitive"] and not can_read_sensitive(user):
        result["value"] = REDACTED_VALUE
    return result


def serialize_config_preview(
    preview: Mapping[str, Any],
    user: UserContext,
    *,
    contains_sensitive_value: bool | None = None,
) -> dict[str, Any]:
    result = dict(preview)
    is_sensitive = bool(result["is_sensitive"])
    should_protect = is_sensitive if contains_sensitive_value is None else contains_sensitive_value
    if should_protect and not can_read_sensitive(user):
        result["value"] = REDACTED_VALUE
    return result


def serialize_history(history: object, user: UserContext) -> dict[str, Any]:
    value = getattr(history, "value")
    if bool(getattr(history, "is_sensitive")) and not can_read_sensitive(user):
        value = REDACTED_VALUE
    return {
        "id": str(getattr(history, "id")),
        "config_id": str(getattr(history, "config_id")),
        "version": getattr(history, "version"),
        "service_name": getattr(history, "service_name"),
        "key": getattr(history, "key"),
        "config_type": getattr(history, "config_type").value,
        "value": value,
        "metadata": getattr(history, "metadata"),
        "is_active": getattr(history, "is_active"),
        "changed_by": getattr(history, "changed_by"),
        "change_reason": getattr(history, "change_reason"),
        "correlation_id": getattr(history, "correlation_id"),
        "changed_at": getattr(history, "changed_at").isoformat(),
    }
