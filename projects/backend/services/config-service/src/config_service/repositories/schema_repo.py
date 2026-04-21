"""Config schema repository."""
from __future__ import annotations

import json
from typing import Any

import asyncpg

from backend_common.repositories.base import BaseRepository
from config_service.domain.enums import ConfigType
from config_service.domain.models import ConfigSchema


class SchemaRepository(BaseRepository):

    async def get_active(self, config_type: ConfigType) -> ConfigSchema | None:
        row = await self._fetchrow(
            "SELECT * FROM config_schemas WHERE config_type = $1 AND is_active = true",
            config_type.value,
        )
        if row is None:
            return None
        return ConfigSchema.from_row(dict(row))

    async def list_active(self) -> list[ConfigSchema]:
        rows = await self._fetch(
            "SELECT * FROM config_schemas WHERE is_active = true ORDER BY config_type"
        )
        return [ConfigSchema.from_row(dict(r)) for r in rows]

    async def list_history(self, config_type: ConfigType) -> list[ConfigSchema]:
        rows = await self._fetch(
            "SELECT * FROM config_schemas WHERE config_type = $1 ORDER BY version DESC",
            config_type.value,
        )
        return [ConfigSchema.from_row(dict(r)) for r in rows]

    async def insert_new_version_and_activate(
        self,
        config_type: ConfigType,
        schema: dict[str, Any],
        created_by: str,
    ) -> ConfigSchema:
        async with self._pool.acquire() as conn:
            async with conn.transaction():
                # Get next version number
                result = await conn.fetchval(
                    "SELECT COALESCE(MAX(version), 0) + 1 FROM config_schemas WHERE config_type = $1",
                    config_type.value,
                )
                new_version: int = result

                # Deactivate current active schema
                await conn.execute(
                    "UPDATE config_schemas SET is_active = false WHERE config_type = $1 AND is_active = true",
                    config_type.value,
                )

                # Insert new active version
                row = await conn.fetchrow(
                    """
                    INSERT INTO config_schemas (config_type, schema, version, is_active, created_by)
                    VALUES ($1, $2::jsonb, $3, true, $4)
                    RETURNING *
                    """,
                    config_type.value, json.dumps(schema), new_version, created_by,
                )
                return ConfigSchema.from_row(dict(row))
