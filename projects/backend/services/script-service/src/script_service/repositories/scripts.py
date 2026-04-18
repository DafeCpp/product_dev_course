"""Script repository backed by asyncpg."""
from __future__ import annotations

import json
from typing import Any
from uuid import UUID

import asyncpg  # type: ignore[import-untyped]

from script_service.domain.models import Script, ScriptType
from script_service.repositories.base import BaseRepository


class ScriptRepository(BaseRepository):
    """CRUD operations for scripts."""

    def __init__(self, pool: asyncpg.Pool) -> None:
        super().__init__(pool)

    @staticmethod
    def _to_model(record: asyncpg.Record) -> Script:
        return Script.from_row(dict(record))

    async def create(
        self,
        name: str,
        description: str | None,
        target_service: str,
        script_type: ScriptType,
        script_body: str,
        parameters_schema: dict[str, Any],
        timeout_sec: int,
        created_by: UUID,
    ) -> Script:
        query = """
            INSERT INTO scripts (
                name, description, target_service, script_type,
                script_body, parameters_schema, timeout_sec, created_by
            )
            VALUES ($1, $2, $3, $4, $5, $6::jsonb, $7, $8)
            RETURNING *
        """
        record = await self._fetchrow(
            query,
            name,
            description,
            target_service,
            str(script_type),
            script_body,
            json.dumps(parameters_schema),
            timeout_sec,
            created_by,
        )
        assert record is not None
        return self._to_model(record)

    async def get_by_id(self, id: UUID) -> Script | None:
        record = await self._fetchrow(
            "SELECT * FROM scripts WHERE id = $1",
            id,
        )
        if record is None:
            return None
        return self._to_model(record)

    async def get_by_name(self, name: str) -> Script | None:
        record = await self._fetchrow(
            "SELECT * FROM scripts WHERE name = $1",
            name,
        )
        if record is None:
            return None
        return self._to_model(record)

    async def list(
        self,
        *,
        target_service: str | None = None,
        is_active: bool | None = None,
        limit: int = 50,
        offset: int = 0,
    ) -> list[Script]:
        conditions: list[str] = []
        params: list[Any] = []
        idx = 1

        if target_service is not None:
            conditions.append(f"target_service = ${idx}")
            params.append(target_service)
            idx += 1

        if is_active is not None:
            conditions.append(f"is_active = ${idx}")
            params.append(is_active)
            idx += 1

        where = f"WHERE {' AND '.join(conditions)}" if conditions else ""
        params.extend([limit, offset])

        query = f"""
            SELECT * FROM scripts
            {where}
            ORDER BY created_at DESC
            LIMIT ${idx} OFFSET ${idx + 1}
        """
        records = await self._fetch(query, *params)
        return [self._to_model(r) for r in records]

    async def update(self, id: UUID, **fields: Any) -> Script | None:
        if not fields:
            return await self.get_by_id(id)

        assignments: list[str] = []
        values: list[Any] = []
        idx = 1

        for column, value in fields.items():
            if column == "parameters_schema":
                assignments.append(f"{column} = ${idx}::jsonb")
                values.append(json.dumps(value))
            elif column == "script_type":
                assignments.append(f"{column} = ${idx}")
                values.append(str(value))
            else:
                assignments.append(f"{column} = ${idx}")
                values.append(value)
            idx += 1

        assignments.append("updated_at = now()")
        values.append(id)

        query = f"""
            UPDATE scripts
            SET {', '.join(assignments)}
            WHERE id = ${idx}
            RETURNING *
        """
        record = await self._fetchrow(query, *values)
        if record is None:
            return None
        return self._to_model(record)

    async def soft_delete(self, id: UUID) -> bool:
        """Set is_active = False. Returns True if record existed."""
        record = await self._fetchrow(
            "UPDATE scripts SET is_active = FALSE, updated_at = now() WHERE id = $1 RETURNING id",
            id,
        )
        return record is not None
