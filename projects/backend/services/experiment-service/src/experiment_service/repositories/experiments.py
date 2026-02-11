"""Experiment repository backed by asyncpg."""
from __future__ import annotations

import json
from datetime import datetime
from typing import Any, List, Tuple
from uuid import UUID

from asyncpg import Pool, Record  # type: ignore[import-untyped]

from experiment_service.core.exceptions import NotFoundError
from experiment_service.domain.dto import ExperimentCreateDTO, ExperimentUpdateDTO
from experiment_service.domain.enums import ExperimentStatus
from experiment_service.domain.models import Experiment
from experiment_service.repositories.base import BaseRepository


class ExperimentRepository(BaseRepository):
    """CRUD operations for experiments."""

    JSONB_COLUMNS = {"metadata"}

    def __init__(self, pool: Pool):
        super().__init__(pool)

    @staticmethod
    def _to_model(record: Record) -> Experiment:
        payload = dict(record)
        for column in ExperimentRepository.JSONB_COLUMNS:
            value = payload.get(column)
            if isinstance(value, str):
                payload[column] = json.loads(value)
        return Experiment.model_validate(payload)

    async def create(self, data: ExperimentCreateDTO) -> Experiment:
        query = """
            INSERT INTO experiments (
                project_id,
                owner_id,
                name,
                description,
                experiment_type,
                tags,
                metadata,
                status
            )
            VALUES ($1, $2, $3, $4, $5, $6, $7::jsonb, $8)
            RETURNING *
        """
        record = await self._fetchrow(
            query,
            data.project_id,
            data.owner_id,
            data.name,
            data.description,
            data.experiment_type,
            data.tags,
            json.dumps(data.metadata),
            data.status.value,
        )
        assert record is not None
        return self._to_model(record)

    async def get(self, project_id: UUID, experiment_id: UUID) -> Experiment:
        record = await self._fetchrow(
            "SELECT * FROM experiments WHERE project_id = $1 AND id = $2",
            project_id,
            experiment_id,
        )
        if record is None:
            raise NotFoundError("Experiment not found")
        return self._to_model(record)

    async def list_by_project(
        self,
        project_id: UUID,
        *,
        limit: int = 50,
        offset: int = 0,
        status: ExperimentStatus | None = None,
        tags: list[str] | None = None,
        created_after: datetime | None = None,
        created_before: datetime | None = None,
    ) -> Tuple[List[Experiment], int]:
        conditions = ["project_id = $1"]
        params: list[Any] = [project_id]
        idx = 2
        if status is not None:
            conditions.append(f"status = ${idx}")
            params.append(status.value)
            idx += 1
        if tags:
            conditions.append(f"tags @> ${idx}")
            params.append(tags)
            idx += 1
        if created_after is not None:
            conditions.append(f"created_at >= ${idx}")
            params.append(created_after)
            idx += 1
        if created_before is not None:
            conditions.append(f"created_at <= ${idx}")
            params.append(created_before)
            idx += 1
        where = " AND ".join(conditions)
        params.extend([limit, offset])
        query = f"""
            SELECT *,
                   COUNT(*) OVER() AS total_count
            FROM experiments
            WHERE {where}
            ORDER BY created_at DESC
            LIMIT ${idx} OFFSET ${idx + 1}
        """
        records = await self._fetch(query, *params)
        items: List[Experiment] = []
        total: int | None = None
        for rec in records:
            rec_dict = dict(rec)
            total_value = rec_dict.pop("total_count", None)
            if total_value is not None:
                total = int(total_value)
            for column in self.JSONB_COLUMNS:
                value = rec_dict.get(column)
                if isinstance(value, str):
                    rec_dict[column] = json.loads(value)
            items.append(Experiment.model_validate(rec_dict))
        if total is None:
            count_query = f"SELECT COUNT(*) AS total FROM experiments WHERE {where}"
            count_params = params[: -(2)]  # exclude limit/offset
            record = await self._fetchrow(count_query, *count_params)
            total = int(record["total"]) if record else 0
        return items, total

    async def search_experiments(
        self,
        project_id: UUID,
        query: str,
        *,
        limit: int = 50,
        offset: int = 0,
    ) -> Tuple[List[Experiment], int]:
        """Search experiments by name and description."""
        search_pattern = f"%{query}%"
        records = await self._fetch(
            """
            SELECT *,
                   COUNT(*) OVER() AS total_count
            FROM experiments
            WHERE project_id = $1
              AND (
                  name ILIKE $2
                  OR description ILIKE $2
              )
            ORDER BY created_at DESC
            LIMIT $3 OFFSET $4
            """,
            project_id,
            search_pattern,
            limit,
            offset,
        )
        items: List[Experiment] = []
        total: int | None = None
        for rec in records:
            rec_dict = dict(rec)
            total_value = rec_dict.pop("total_count", None)
            if total_value is not None:
                total = int(total_value)
            for column in self.JSONB_COLUMNS:
                value = rec_dict.get(column)
                if isinstance(value, str):
                    rec_dict[column] = json.loads(value)
            items.append(Experiment.model_validate(rec_dict))
        if total is None:
            # Fallback count if window function didn't work
            count_record = await self._fetchrow(
                """
                SELECT COUNT(*) AS total
                FROM experiments
                WHERE project_id = $1
                  AND (
                      name ILIKE $2
                      OR description ILIKE $2
                  )
                """,
                project_id,
                search_pattern,
            )
            total = int(count_record["total"]) if count_record else 0
        return items, total

    async def update(
        self,
        project_id: UUID,
        experiment_id: UUID,
        updates: ExperimentUpdateDTO,
    ) -> Experiment:
        payload = updates.model_dump(exclude_none=True)
        if not payload:
            raise ValueError("No fields provided for update")

        assignments = []
        values: list[Any] = []
        idx = 1
        for column, value in payload.items():
            column_expr = f"{column} = ${idx}"
            if column in self.JSONB_COLUMNS:
                column_expr += "::jsonb"
            assignments.append(column_expr)
            if column in self.JSONB_COLUMNS:
                values.append(json.dumps(value))  # type: ignore[arg-type]
            else:
                values.append(value)
            idx += 1
        assignments.append("updated_at = now()")
        values.extend([project_id, experiment_id])

        query = f"""
            UPDATE experiments
            SET {', '.join(assignments)}
            WHERE project_id = ${idx} AND id = ${idx + 1}
            RETURNING *
        """
        record = await self._fetchrow(query, *values)
        if record is None:
            raise NotFoundError("Experiment not found")
        return self._to_model(record)

    async def delete(self, project_id: UUID, experiment_id: UUID) -> None:
        record = await self._fetchrow(
            "DELETE FROM experiments WHERE project_id = $1 AND id = $2 RETURNING id",
            project_id,
            experiment_id,
        )
        if record is None:
            raise NotFoundError("Experiment not found")

