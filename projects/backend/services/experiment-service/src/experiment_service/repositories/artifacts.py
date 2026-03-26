"""Artifact repository layer."""
from __future__ import annotations

import json
from typing import Any
from uuid import UUID

from asyncpg import Pool, Record  # type: ignore[import-untyped]

from experiment_service.domain.models import Artifact
from experiment_service.repositories.base import BaseRepository


class ArtifactRepository(BaseRepository):
    """CRUD helpers for artifacts."""

    JSONB_COLUMNS = {"metadata"}

    def __init__(self, pool: Pool) -> None:
        super().__init__(pool)

    @staticmethod
    def _to_model(record: Record) -> Artifact:
        payload = dict(record)
        value = payload.get("metadata")
        if isinstance(value, str):
            payload["metadata"] = json.loads(value)
        return Artifact.model_validate(payload)

    async def create(
        self,
        *,
        run_id: UUID,
        project_id: UUID,
        type: str,
        uri: str,
        created_by: UUID,
        checksum: str | None = None,
        size_bytes: int | None = None,
        metadata: dict[str, Any] | None = None,
        is_restricted: bool = False,
    ) -> Artifact:
        query = """
            INSERT INTO artifacts (
                run_id,
                project_id,
                type,
                uri,
                checksum,
                size_bytes,
                metadata,
                created_by,
                is_restricted
            )
            VALUES ($1, $2, $3, $4, $5, $6, $7::jsonb, $8, $9)
            RETURNING *
        """
        record = await self._fetchrow(
            query,
            run_id,
            project_id,
            type,
            uri,
            checksum,
            size_bytes,
            json.dumps(metadata or {}),
            created_by,
            is_restricted,
        )
        assert record is not None
        return self._to_model(record)

    async def get(self, artifact_id: UUID) -> Artifact | None:
        record = await self._fetchrow(
            "SELECT * FROM artifacts WHERE id = $1",
            artifact_id,
        )
        if record is None:
            return None
        return self._to_model(record)

    async def list_by_run(
        self,
        run_id: UUID,
        *,
        type_filter: str | None = None,
        limit: int = 50,
        offset: int = 0,
    ) -> tuple[list[Artifact], int]:
        params: list[Any] = [run_id]
        conditions = ["run_id = $1"]
        idx = 2
        if type_filter is not None:
            conditions.append(f"type = ${idx}")
            params.append(type_filter)
            idx += 1
        where = " AND ".join(conditions)
        params.extend([limit, offset])
        query = f"""
            SELECT *,
                   COUNT(*) OVER() AS total_count
            FROM artifacts
            WHERE {where}
            ORDER BY created_at DESC
            LIMIT ${idx} OFFSET ${idx + 1}
        """
        records = await self._fetch(query, *params)
        items: list[Artifact] = []
        total: int | None = None
        for rec in records:
            rec_dict = dict(rec)
            total_value = rec_dict.pop("total_count", None)
            if total_value is not None:
                total = int(total_value)
            value = rec_dict.get("metadata")
            if isinstance(value, str):
                rec_dict["metadata"] = json.loads(value)
            items.append(Artifact.model_validate(rec_dict))
        if total is None:
            count_params = params[: -(2)]
            record = await self._fetchrow(
                f"SELECT COUNT(*) AS total FROM artifacts WHERE {where}",
                *count_params,
            )
            total = int(record["total"]) if record else 0
        return items, total

    async def delete(self, artifact_id: UUID) -> bool:
        record = await self._fetchrow(
            "DELETE FROM artifacts WHERE id = $1 RETURNING id",
            artifact_id,
        )
        return record is not None

    async def approve(self, artifact_id: UUID, user_id: UUID, note: str | None = None) -> Artifact | None:
        record = await self._fetchrow(
            """
            UPDATE artifacts
            SET approved_by = $2,
                approval_note = $3,
                updated_at = now()
            WHERE id = $1
            RETURNING *
            """,
            artifact_id,
            user_id,
            note,
        )
        if record is None:
            return None
        return self._to_model(record)
