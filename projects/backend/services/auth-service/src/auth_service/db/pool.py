"""Asyncpg connection pool helpers."""
from __future__ import annotations

import asyncpg  # type: ignore[import-untyped]

from typing import Any

from auth_service.settings import settings

pool: asyncpg.Pool | None = None


async def init_pool(_app: Any = None) -> None:
    """Initialize global asyncpg pool."""
    global pool
    if pool is None:
        pool = await asyncpg.create_pool(
            dsn=str(settings.database_url),
            max_size=settings.db_pool_size,
        )


async def close_pool(_app: Any = None) -> None:
    """Close pool on shutdown."""
    global pool
    if pool is not None:
        await pool.close()
        pool = None


def get_pool() -> asyncpg.Pool:
    """Get the global pool (raises if not initialized)."""
    if pool is None:
        raise RuntimeError("Database pool not initialized")
    return pool

