"""Audit log (capture_session_events) service."""
from __future__ import annotations

from typing import Any, List
from uuid import UUID

from experiment_service.domain.models import CaptureSessionEvent
from experiment_service.repositories.capture_session_events import CaptureSessionEventRepository


class CaptureSessionEventService:
    """Read/write operations for capture session audit events."""

    def __init__(self, repository: CaptureSessionEventRepository):
        self._repository = repository

    async def record_event(
        self,
        *,
        capture_session_id: UUID,
        event_type: str,
        actor_id: UUID,
        actor_role: str,
        payload: dict[str, Any] | None = None,
    ) -> CaptureSessionEvent:
        return await self._repository.create(
            capture_session_id=capture_session_id,
            event_type=event_type,
            actor_id=actor_id,
            actor_role=actor_role,
            payload=payload,
        )

    async def list_events(
        self,
        capture_session_id: UUID,
        *,
        limit: int = 50,
        offset: int = 0,
    ) -> tuple[List[CaptureSessionEvent], int]:
        return await self._repository.list_by_session(
            capture_session_id,
            limit=limit,
            offset=offset,
        )

