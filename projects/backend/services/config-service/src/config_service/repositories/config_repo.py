"""Config repository — CRUD with optimistic locking and history in one transaction."""
from __future__ import annotations

import json
from typing import Any
from uuid import UUID

import asyncpg

from backend_common.repositories.base import BaseRepository
from config_service.domain.enums import ConfigType
from config_service.domain.models import Config


class ConfigRepository(BaseRepository):

    async def create(
        self,
        *,
        service_name: str,
        project_id: str | None,
        key: str,
        config_type: ConfigType,
        description: str | None,
        value: dict[str, Any],
        metadata: dict[str, Any],
        is_critical: bool,
        is_sensitive: bool,
        created_by: str,
        change_reason: str,
        source_ip: str | None,
        user_agent: str | None,
        correlation_id: str | None,
    ) -> Config:
        async with self._pool.acquire() as conn:
            async with conn.transaction():
                row = await conn.fetchrow(
                    """
                    INSERT INTO configs (
                        service_name, project_id, key, config_type, description,
                        value, metadata, is_critical, is_sensitive, is_active,
                        version, created_by, updated_by
                    ) VALUES ($1,$2,$3,$4,$5,$6::jsonb,$7::jsonb,$8,$9,true,1,$10,$10)
                    RETURNING *
                    """,
                    service_name, project_id, key, config_type.value, description,
                    json.dumps(value), json.dumps(metadata),
                    is_critical, is_sensitive, created_by,
                )
                config = Config.from_row(dict(row))
                await self._insert_history(
                    conn,
                    config=config,
                    changed_by=created_by,
                    change_reason=change_reason or "Initial creation",
                    source_ip=source_ip,
                    user_agent=user_agent,
                    correlation_id=correlation_id,
                )
                return config

    async def get_by_id(self, config_id: UUID) -> Config | None:
        row = await self._fetchrow(
            "SELECT * FROM configs WHERE id = $1 AND deleted_at IS NULL",
            config_id,
        )
        if row is None:
            return None
        return Config.from_row(dict(row))

    async def list_by_filters(
        self,
        *,
        service_name: str | None = None,
        project_id: str | None = None,
        config_type: ConfigType | None = None,
        is_active: bool | None = None,
        limit: int = 50,
        cursor: str | None = None,
    ) -> tuple[list[Config], str | None]:
        conditions = ["deleted_at IS NULL"]
        args: list[Any] = []
        idx = 1

        if service_name is not None:
            conditions.append(f"service_name = ${idx}")
            args.append(service_name)
            idx += 1
        if project_id is not None:
            conditions.append(f"project_id = ${idx}")
            args.append(project_id)
            idx += 1
        if config_type is not None:
            conditions.append(f"config_type = ${idx}")
            args.append(config_type.value)
            idx += 1
        if is_active is not None:
            conditions.append(f"is_active = ${idx}")
            args.append(is_active)
            idx += 1
        if cursor is not None:
            conditions.append(f"id > ${idx}::uuid")
            args.append(cursor)
            idx += 1

        where = " AND ".join(conditions)
        args.append(limit + 1)
        rows = await self._fetch(
            f"SELECT * FROM configs WHERE {where} ORDER BY id LIMIT ${idx}",
            *args,
        )
        items = [Config.from_row(dict(r)) for r in rows]

        next_cursor: str | None = None
        if len(items) > limit:
            items = items[:limit]
            next_cursor = str(items[-1].id)

        return items, next_cursor

    async def update_with_version(
        self,
        *,
        config_id: UUID,
        expected_version: int,
        changed_by: str,
        change_reason: str,
        source_ip: str | None,
        user_agent: str | None,
        correlation_id: str | None,
        description: str | None = None,
        value: dict[str, Any] | None = None,
        metadata: dict[str, Any] | None = None,
        is_active: bool | None = None,
        is_critical: bool | None = None,
        is_sensitive: bool | None = None,
    ) -> Config | None:
        """Returns None if version conflict (0 rows updated)."""
        set_parts = ["version = version + 1", "updated_by = $3", "updated_at = NOW()"]
        args: list[Any] = [config_id, expected_version, changed_by]
        idx = 4

        if description is not None:
            set_parts.append(f"description = ${idx}")
            args.append(description)
            idx += 1
        if value is not None:
            set_parts.append(f"value = ${idx}::jsonb")
            args.append(json.dumps(value))
            idx += 1
        if metadata is not None:
            set_parts.append(f"metadata = ${idx}::jsonb")
            args.append(json.dumps(metadata))
            idx += 1
        if is_active is not None:
            set_parts.append(f"is_active = ${idx}")
            args.append(is_active)
            idx += 1
        if is_critical is not None:
            set_parts.append(f"is_critical = ${idx}")
            args.append(is_critical)
            idx += 1
        if is_sensitive is not None:
            set_parts.append(f"is_sensitive = ${idx}")
            args.append(is_sensitive)
            idx += 1

        set_clause = ", ".join(set_parts)

        async with self._pool.acquire() as conn:
            async with conn.transaction():
                row = await conn.fetchrow(
                    f"""
                    UPDATE configs
                    SET {set_clause}
                    WHERE id = $1 AND version = $2 AND deleted_at IS NULL
                    RETURNING *
                    """,
                    *args,
                )
                if row is None:
                    return None
                config = Config.from_row(dict(row))
                await self._insert_history(
                    conn,
                    config=config,
                    changed_by=changed_by,
                    change_reason=change_reason,
                    source_ip=source_ip,
                    user_agent=user_agent,
                    correlation_id=correlation_id,
                )
                return config

    async def soft_delete(
        self,
        *,
        config_id: UUID,
        expected_version: int,
        deleted_by: str,
        change_reason: str,
        source_ip: str | None,
        user_agent: str | None,
        correlation_id: str | None,
    ) -> Config | None:
        async with self._pool.acquire() as conn:
            async with conn.transaction():
                row = await conn.fetchrow(
                    """
                    UPDATE configs
                    SET deleted_at = NOW(), version = version + 1,
                        updated_by = $3, updated_at = NOW()
                    WHERE id = $1 AND version = $2 AND deleted_at IS NULL
                    RETURNING *
                    """,
                    config_id, expected_version, deleted_by,
                )
                if row is None:
                    return None
                config = Config.from_row(dict(row))
                await self._insert_history(
                    conn,
                    config=config,
                    changed_by=deleted_by,
                    change_reason=change_reason,
                    source_ip=source_ip,
                    user_agent=user_agent,
                    correlation_id=correlation_id,
                )
                return config

    async def list_active_by_service(
        self,
        service_name: str,
        project_id: str | None,
    ) -> list[Config]:
        if project_id is not None:
            rows = await self._fetch(
                """
                SELECT * FROM configs
                WHERE service_name = $1 AND project_id = $2
                  AND is_active = true AND deleted_at IS NULL
                """,
                service_name, project_id,
            )
        else:
            rows = await self._fetch(
                """
                SELECT * FROM configs
                WHERE service_name = $1 AND project_id IS NULL
                  AND is_active = true AND deleted_at IS NULL
                """,
                service_name,
            )
        return [Config.from_row(dict(r)) for r in rows]

    async def list_by_type(self, config_type: ConfigType) -> list[Config]:
        rows = await self._fetch(
            "SELECT * FROM configs WHERE config_type = $1 AND deleted_at IS NULL",
            config_type.value,
        )
        return [Config.from_row(dict(r)) for r in rows]

    @staticmethod
    async def _insert_history(
        conn: asyncpg.Connection,
        *,
        config: Config,
        changed_by: str,
        change_reason: str,
        source_ip: str | None,
        user_agent: str | None,
        correlation_id: str | None,
    ) -> None:
        await conn.execute(
            """
            INSERT INTO config_history (
                config_id, version, service_name, key, config_type,
                value, metadata, is_active,
                changed_by, change_reason, source_ip, user_agent, correlation_id
            ) VALUES ($1,$2,$3,$4,$5,$6::jsonb,$7::jsonb,$8,$9,$10,$11,$12,$13)
            """,
            config.id, config.version, config.service_name, config.key,
            config.config_type.value,
            json.dumps(config.value), json.dumps(config.metadata),
            config.is_active,
            changed_by, change_reason, source_ip, user_agent, correlation_id,
        )
