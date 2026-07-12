"""Experiment-service adapter for the shared idempotency repository."""
from __future__ import annotations

from datetime import datetime

from backend_common.idempotency import IdempotencyRecord, IdempotencyRepository as CommonIdempotencyRepository


class IdempotencyRepository(CommonIdempotencyRepository):
    async def delete_expired(self, created_before: datetime | None = None) -> int:  # type: ignore[override]
        if created_before is None:
            return await super().delete_expired()
        result = await self._execute(
            f"DELETE FROM {self._table_name} WHERE created_at < $1",
            created_before,
        )
        return int(result.split()[-1])


__all__ = ["IdempotencyRecord", "IdempotencyRepository"]
