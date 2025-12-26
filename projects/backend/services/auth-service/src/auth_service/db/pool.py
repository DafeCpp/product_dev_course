"""Asyncpg connection pool helpers."""
from __future__ import annotations

import asyncpg  # type: ignore[import-untyped]

from typing import Any

from backend_common.db.pool import (
    close_pool as _close_pool,
    get_pool as _get_pool,
    init_pool as _init_pool,
)

from auth_service.settings import settings

# Local reference to the pool for sync access
_local_pool: asyncpg.Pool | None = None


async def init_pool(_app: Any = None) -> None:
    """Initialize global asyncpg pool."""
    global _local_pool
    await _init_pool(str(settings.database_url), settings.db_pool_size, _app)
    # Store reference for sync access
    _local_pool = await _get_pool()


async def close_pool(_app: Any = None) -> None:
    """Close pool on shutdown."""
    global _local_pool
    await _close_pool(_app)
    _local_pool = None


def get_pool() -> asyncpg.Pool:
    """Get the global pool (raises if not initialized)."""
    if _local_pool is None:
        raise RuntimeError("Database pool not initialized")
    return _local_pool

