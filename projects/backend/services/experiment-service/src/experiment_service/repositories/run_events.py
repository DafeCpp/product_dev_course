"""Run event (audit log) repository."""
from __future__ import annotations

import json
from typing import Any, List, Tuple
from uuid import UUID

from asyncpg import Pool, Record  # type: ignore[import-untyped]

from experiment_service.domain.models import RunEvent
from backend_common.repositories.base import BaseRepository


class RunEventRepository(BaseRepository):
    """CRUD helpers for run_events."""

    def __init__(self, pool: Pool):
        super().__init__(pool)

    @staticmethod
    def _to_model(record: Record) -> RunEvent:
        payload = dict(record)
        value = payload.get("payload")
        if isinstance(value, str):
            payload["payload"] = json.loads(value)
        return RunEvent.model_validate(payload)

    async def create(
        self,
        *,
        run_id: UUID,
        event_type: str,
        actor_id: UUID,
        actor_role: str,
        payload: dict[str, Any] | None = None,
    ) -> RunEvent:
        record = await self._fetchrow(
            """
            INSERT INTO run_events (
                run_id,
                event_type,
                actor_id,
                actor_role,
                payload
            )
            VALUES ($1, $2, $3, $4, $5::jsonb)
            RETURNING *
            """,
            run_id,
            event_type,
            actor_id,
            actor_role,
            json.dumps(payload or {}),
        )
        assert record is not None
        return self._to_model(record)

    async def list_by_run(
        self,
        run_id: UUID,
        *,
        limit: int = 50,
        offset: int = 0,
    ) -> Tuple[List[RunEvent], int]:
        records = await self._fetch(
            """
            SELECT *,
                   COUNT(*) OVER() AS total_count
            FROM run_events
            WHERE run_id = $1
            ORDER BY created_at ASC, id ASC
            LIMIT $2 OFFSET $3
            """,
            run_id,
            limit,
            offset,
        )
        items: List[RunEvent] = []
        total: int | None = None
        for rec in records:
            rec_dict = dict(rec)
            total_value = rec_dict.pop("total_count", None)
            if total_value is not None:
                total = int(total_value)
            value = rec_dict.get("payload")
            if isinstance(value, str):
                rec_dict["payload"] = json.loads(value)
            items.append(RunEvent.model_validate(rec_dict))
        if total is None:
            total = await self._count_by_run(run_id)
        return items, total

    async def _count_by_run(self, run_id: UUID) -> int:
        record = await self._fetchrow(
            "SELECT COUNT(*) AS total FROM run_events WHERE run_id = $1",
            run_id,
        )
        return int(record["total"]) if record else 0
