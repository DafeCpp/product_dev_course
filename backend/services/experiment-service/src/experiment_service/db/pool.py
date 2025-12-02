"""Asyncpg connection pool helpers."""
from __future__ import annotations

import asyncpg  # type: ignore[import-untyped]

from typing import Any, AsyncIterator

from experiment_service.settings import settings

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


async def get_pool() -> asyncpg.Pool:
    """Return the initialized asyncpg pool, creating it if needed."""
    global pool
    if pool is None:
        await init_pool()
    assert pool is not None  # for type checkers
    return pool


async def get_connection() -> AsyncIterator[asyncpg.Connection]:
    """Yield a connection from the global pool."""
    pool = await get_pool()
    async with pool.acquire() as conn:
        yield conn

