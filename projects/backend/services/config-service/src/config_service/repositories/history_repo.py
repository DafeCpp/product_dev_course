"""Config history repository."""
from __future__ import annotations

from uuid import UUID

from backend_common.repositories.base import BaseRepository
from config_service.domain.models import ConfigHistory


class HistoryRepository(BaseRepository):

    async def get_by_version(self, config_id: UUID, version: int) -> ConfigHistory | None:
        row = await self._fetchrow(
            "SELECT * FROM config_history WHERE config_id = $1 AND version = $2",
            config_id, version,
        )
        if row is None:
            return None
        return ConfigHistory.from_row(dict(row))

    async def list_by_config_id(
        self,
        config_id: UUID,
        limit: int = 50,
        offset: int = 0,
    ) -> list[ConfigHistory]:
        rows = await self._fetch(
            """
            SELECT * FROM config_history
            WHERE config_id = $1
            ORDER BY version DESC
            LIMIT $2 OFFSET $3
            """,
            config_id, limit, offset,
        )
        return [ConfigHistory.from_row(dict(r)) for r in rows]
