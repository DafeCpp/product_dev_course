"""Structured audit logging for config changes."""
from __future__ import annotations

from typing import Any

import structlog

logger = structlog.get_logger("audit")


class AuditService:
    def log(
        self,
        *,
        action: str,
        actor: str,
        service_name: str,
        config_type: str,
        config_id: str,
        key: str,
        change_reason: str | None,
        is_critical: bool,
        is_sensitive: bool,
        correlation_id: str | None,
        source_ip: str | None,
        user_agent: str | None,
        value: Any = None,
    ) -> None:
        redacted_value = "***" if is_sensitive else value
        logger.info(
            "config_audit",
            action=action,
            actor=actor,
            service_name=service_name,
            config_type=config_type,
            config_id=config_id,
            key=key,
            change_reason=change_reason,
            is_critical=is_critical,
            value=redacted_value,
            correlation_id=correlation_id,
            source_ip=source_ip,
            user_agent=user_agent,
        )
