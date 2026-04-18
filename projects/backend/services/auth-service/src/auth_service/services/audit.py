"""Audit service."""
from __future__ import annotations

from datetime import datetime
from typing import Any
from uuid import UUID

from auth_service.domain.models import AuditEntry
from auth_service.repositories.audit import AuditRepository


class AuditService:
    """Service for recording and querying the audit log."""

    def __init__(self, audit_repo: AuditRepository) -> None:
        self._repo = audit_repo

    async def log_action(
        self,
        actor_id: UUID,
        action: str,
        scope_type: str,
        *,
        scope_id: UUID | None = None,
        target_type: str | None = None,
        target_id: str | None = None,
        details: dict[str, Any] | None = None,
        ip_address: str | None = None,
        user_agent: str | None = None,
    ) -> AuditEntry:
        """Record an audit log entry."""
        return await self._repo.log(
            actor_id=actor_id,
            action=action,
            scope_type=scope_type,
            scope_id=scope_id,
            target_type=target_type,
            target_id=target_id,
            details=details,
            ip_address=ip_address,
            user_agent=user_agent,
        )

    async def query(
        self,
        *,
        actor_id: UUID | None = None,
        action: str | None = None,
        scope_type: str | None = None,
        scope_id: UUID | None = None,
        target_type: str | None = None,
        target_id: str | None = None,
        from_date: datetime | None = None,
        to_date: datetime | None = None,
        limit: int = 50,
        offset: int = 0,
    ) -> list[AuditEntry]:
        """Query audit log with filters and pagination."""
        return await self._repo.query(
            actor_id=actor_id,
            action=action,
            scope_type=scope_type,
            scope_id=scope_id,
            target_type=target_type,
            target_id=target_id,
            from_date=from_date,
            to_date=to_date,
            limit=limit,
            offset=offset,
        )
