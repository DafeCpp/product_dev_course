"""Experiment-service adapter for the shared idempotency repository."""
from __future__ import annotations

from typing import Any

from backend_common.idempotency import (
    IdempotencyRecord,
    IdempotencyRepository as CommonIdempotencyRepository,
)

TABLE_NAME = "request_idempotency"


class IdempotencyRepository(CommonIdempotencyRepository):
    """Shared repository pointed at this service's table name."""

    def __init__(self, pool: Any) -> None:
        super().__init__(pool, table_name=TABLE_NAME)


__all__ = ["TABLE_NAME", "IdempotencyRecord", "IdempotencyRepository"]
