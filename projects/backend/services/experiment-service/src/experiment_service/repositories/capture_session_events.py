"""Capture session event (audit log) repository."""
from __future__ import annotations

import json
from typing import Any, List, Tuple
from uuid import UUID

from asyncpg import Pool, Record  # type: ignore[import-untyped]

from experiment_service.domain.models import CaptureSessionEvent
from experiment_service.repositories.base import BaseRepository


class CaptureSessionEventRepository(BaseRepository):
    """CRUD helpers for capture_session_events."""

    def __init__(self, pool: Pool):
        super().__init__(pool)

    @staticmethod
    def _to_model(record: Record) -> CaptureSessionEvent:
        payload = dict(record)
        value = payload.get("payload")
        if isinstance(value, str):
            payload["payload"] = json.loads(value)
        return CaptureSessionEvent.model_validate(payload)

    async def create(
        self,
        *,
        capture_session_id: UUID,
        event_type: str,
        actor_id: UUID,
        actor_role: str,
        payload: dict[str, Any] | None = None,
    ) -> CaptureSessionEvent:
        record = await self._fetchrow(
            """
            INSERT INTO capture_session_events (
                capture_session_id,
                event_type,
                actor_id,
                actor_role,
                payload
            )
            VALUES ($1, $2, $3, $4, $5::jsonb)
            RETURNING *
            """,
            capture_session_id,
            event_type,
            actor_id,
            actor_role,
            json.dumps(payload or {}),
        )
        assert record is not None
        return self._to_model(record)

    async def list_by_session(
        self,
        capture_session_id: UUID,
        *,
        limit: int = 50,
        offset: int = 0,
    ) -> Tuple[List[CaptureSessionEvent], int]:
        records = await self._fetch(
            """
            SELECT *,
                   COUNT(*) OVER() AS total_count
            FROM capture_session_events
            WHERE capture_session_id = $1
            ORDER BY created_at ASC, id ASC
            LIMIT $2 OFFSET $3
            """,
            capture_session_id,
            limit,
            offset,
        )
        items: List[CaptureSessionEvent] = []
        total: int | None = None
        for rec in records:
            rec_dict = dict(rec)
            total_value = rec_dict.pop("total_count", None)
            if total_value is not None:
                total = int(total_value)
            value = rec_dict.get("payload")
            if isinstance(value, str):
                rec_dict["payload"] = json.loads(value)
            items.append(CaptureSessionEvent.model_validate(rec_dict))
        if total is None:
            total = await self._count_by_session(capture_session_id)
        return items, total

    async def _count_by_session(self, capture_session_id: UUID) -> int:
        record = await self._fetchrow(
            "SELECT COUNT(*) AS total FROM capture_session_events WHERE capture_session_id = $1",
            capture_session_id,
        )
        return int(record["total"]) if record else 0

