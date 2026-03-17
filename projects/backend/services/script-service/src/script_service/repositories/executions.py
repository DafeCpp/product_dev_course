"""Execution repository backed by asyncpg."""
from __future__ import annotations

import json
from datetime import datetime
from typing import Any
from uuid import UUID

import asyncpg  # type: ignore[import-untyped]

from script_service.domain.models import ExecutionStatus, ScriptExecution
from script_service.repositories.base import BaseRepository


class ExecutionRepository(BaseRepository):
    """CRUD operations for script executions."""

    def __init__(self, pool: asyncpg.Pool) -> None:
        super().__init__(pool)

    @staticmethod
    def _to_model(record: asyncpg.Record) -> ScriptExecution:
        return ScriptExecution.from_row(dict(record))

    async def create(
        self,
        script_id: UUID,
        parameters: dict[str, Any],
        requested_by: UUID,
        target_instance: str | None = None,
    ) -> ScriptExecution:
        query = """
            INSERT INTO script_executions (
                script_id, parameters, requested_by, target_instance
            )
            VALUES ($1, $2::jsonb, $3, $4)
            RETURNING *
        """
        record = await self._fetchrow(
            query,
            script_id,
            json.dumps(parameters),
            requested_by,
            target_instance,
        )
        assert record is not None
        return self._to_model(record)

    async def get_by_id(self, id: UUID) -> ScriptExecution | None:
        record = await self._fetchrow(
            "SELECT * FROM script_executions WHERE id = $1",
            id,
        )
        if record is None:
            return None
        return self._to_model(record)

    async def list(
        self,
        *,
        script_id: UUID | None = None,
        status: ExecutionStatus | None = None,
        requested_by: UUID | None = None,
        limit: int = 50,
        offset: int = 0,
    ) -> list[ScriptExecution]:
        conditions: list[str] = []
        params: list[Any] = []
        idx = 1

        if script_id is not None:
            conditions.append(f"script_id = ${idx}")
            params.append(script_id)
            idx += 1

        if status is not None:
            conditions.append(f"status = ${idx}")
            params.append(str(status))
            idx += 1

        if requested_by is not None:
            conditions.append(f"requested_by = ${idx}")
            params.append(requested_by)
            idx += 1

        where = f"WHERE {' AND '.join(conditions)}" if conditions else ""
        params.extend([limit, offset])

        query = f"""
            SELECT * FROM script_executions
            {where}
            ORDER BY created_at DESC
            LIMIT ${idx} OFFSET ${idx + 1}
        """
        records = await self._fetch(query, *params)
        return [self._to_model(r) for r in records]

    async def update_status(
        self,
        id: UUID,
        status: ExecutionStatus,
        *,
        started_at: datetime | None = None,
        finished_at: datetime | None = None,
        exit_code: int | None = None,
        stdout: str | None = None,
        stderr: str | None = None,
        error_message: str | None = None,
    ) -> ScriptExecution | None:
        extra: dict[str, Any] = {}
        if started_at is not None:
            extra["started_at"] = started_at
        if finished_at is not None:
            extra["finished_at"] = finished_at
        if exit_code is not None:
            extra["exit_code"] = exit_code
        if stdout is not None:
            extra["stdout"] = stdout
        if stderr is not None:
            extra["stderr"] = stderr
        if error_message is not None:
            extra["error_message"] = error_message

        assignments: list[str] = ["status = $1"]
        values: list[Any] = [str(status)]
        idx = 2

        for column, value in extra.items():
            assignments.append(f"{column} = ${idx}")
            values.append(value)
            idx += 1

        assignments.append("updated_at = now()")
        values.append(id)

        query = f"""
            UPDATE script_executions
            SET {', '.join(assignments)}
            WHERE id = ${idx}
            RETURNING *
        """
        record = await self._fetchrow(query, *values)
        if record is None:
            return None
        return self._to_model(record)
