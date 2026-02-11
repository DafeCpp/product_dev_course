"""Asyncpg connection pool helpers."""
from __future__ import annotations

import asyncpg  # type: ignore[import-untyped]

from typing import Any, AsyncIterator, Protocol


pool: asyncpg.Pool | None = None
_sync_pool: asyncpg.Pool | None = None  # For sync access in auth-service


class SettingsProtocol(Protocol):
    """Protocol for settings objects with database configuration."""

    database_url: Any
    db_pool_size: int


async def init_pool(database_url: str, pool_size: int, _app: Any = None) -> None:
    """Initialize global asyncpg pool."""
    global pool, _sync_pool
    if pool is None:
        pool = await asyncpg.create_pool(
            dsn=database_url,
            max_size=pool_size,
        )
        _sync_pool = pool


async def close_pool(_app: Any = None) -> None:
    """Close pool on shutdown."""
    global pool, _sync_pool
    if pool is not None:
        await pool.close()
        pool = None
        _sync_pool = None


async def get_pool() -> asyncpg.Pool:
    """Return the initialized asyncpg pool, creating it if needed."""
    global pool
    if pool is None:
        raise RuntimeError("Database pool not initialized. Call init_pool() first.")
    assert pool is not None  # for type checkers
    return pool


def get_pool_sync() -> asyncpg.Pool:
    """Return the initialized asyncpg pool synchronously (raises if not initialized)."""
    global _sync_pool
    if _sync_pool is None:
        raise RuntimeError("Database pool not initialized")
    return _sync_pool


async def get_connection() -> AsyncIterator[asyncpg.Connection]:
    """Yield a connection from the global pool."""
    pool = await get_pool()
    async with pool.acquire() as conn:
        yield conn


def create_pool_wrappers(settings: SettingsProtocol) -> tuple[
    Any,
    Any,
]:
    """Create init_pool and close_pool wrappers for a service.

    Args:
        settings: Settings object with database_url and db_pool_size attributes

    Returns:
        Tuple of (init_pool_wrapper, close_pool_wrapper) functions
    """
    async def init_pool_wrapper(_app: Any = None) -> None:
        """Initialize global asyncpg pool using service settings."""
        await init_pool(str(settings.database_url), settings.db_pool_size, _app)

    async def close_pool_wrapper(_app: Any = None) -> None:
        """Close pool on shutdown."""
        await close_pool(_app)

    return init_pool_wrapper, close_pool_wrapper


async def init_pool_service(_app: Any = None, settings: SettingsProtocol | None = None) -> None:
    """Initialize pool using provided settings.

    Args:
        _app: Optional application object (for compatibility with aiohttp lifecycle)
        settings: Settings object with database_url and db_pool_size.
    """
    if settings is None:
        raise RuntimeError(
            "settings parameter is required. Pass the service settings explicitly."
        )
    await init_pool(str(settings.database_url), settings.db_pool_size, _app)


async def close_pool_service(_app: Any = None) -> None:
    """Close pool on shutdown (compatibility wrapper)."""
    await close_pool(_app)


async def get_pool_service() -> asyncpg.Pool:
    """Get pool (async version) - compatibility wrapper."""
    return await get_pool()


def get_pool_service_sync() -> asyncpg.Pool:
    """Get pool (sync version) - compatibility wrapper."""
    return get_pool_sync()

