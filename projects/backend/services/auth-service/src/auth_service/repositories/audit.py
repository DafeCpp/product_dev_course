"""Audit log repository."""
from __future__ import annotations

import json
from datetime import datetime
from typing import Any
from uuid import UUID

from auth_service.domain.models import AuditEntry
from auth_service.repositories.base import BaseRepository


class AuditRepository(BaseRepository):
    """Append-only repository for audit log entries."""

    async def log(
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
        """Write a single audit log entry."""
        row = await self._fetchrow(
            "INSERT INTO audit_log "
            "(actor_id, action, scope_type, scope_id, target_type, target_id, "
            " details, ip_address, user_agent) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8::inet, $9) "
            "RETURNING id, timestamp, actor_id, action, scope_type, scope_id, "
            "target_type, target_id, details, ip_address, user_agent",
            actor_id, action, scope_type, scope_id, target_type, target_id,
            json.dumps(details or {}), ip_address, user_agent,
        )
        assert row is not None
        return AuditEntry.from_row(dict(row))

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
        """Query audit log with filters."""
        conditions: list[str] = []
        params: list[object] = []
        idx = 1

        if actor_id is not None:
            conditions.append(f"actor_id = ${idx}")
            params.append(actor_id)
            idx += 1

        if action is not None:
            conditions.append(f"action = ${idx}")
            params.append(action)
            idx += 1

        if scope_type is not None:
            conditions.append(f"scope_type = ${idx}")
            params.append(scope_type)
            idx += 1

        if scope_id is not None:
            conditions.append(f"scope_id = ${idx}")
            params.append(scope_id)
            idx += 1

        if target_type is not None:
            conditions.append(f"target_type = ${idx}")
            params.append(target_type)
            idx += 1

        if target_id is not None:
            conditions.append(f"target_id = ${idx}")
            params.append(target_id)
            idx += 1

        if from_date is not None:
            conditions.append(f"timestamp >= ${idx}")
            params.append(from_date)
            idx += 1

        if to_date is not None:
            conditions.append(f"timestamp <= ${idx}")
            params.append(to_date)
            idx += 1

        where = f"WHERE {' AND '.join(conditions)}" if conditions else ""

        params.append(limit)
        limit_idx = idx
        idx += 1
        params.append(offset)
        offset_idx = idx

        query = (
            "SELECT id, timestamp, actor_id, action, scope_type, scope_id, "
            "target_type, target_id, details, ip_address, user_agent "
            f"FROM audit_log {where} "
            f"ORDER BY timestamp DESC "
            f"LIMIT ${limit_idx} OFFSET ${offset_idx}"
        )

        rows = await self._fetch(query, *params)
        return [AuditEntry.from_row(dict(r)) for r in rows]
