"""Shared asyncpg helpers for repositories."""
from __future__ import annotations

from typing import Any, Iterable

import asyncpg  # type: ignore[import-untyped]


class BaseRepository:
    """Thin wrapper over asyncpg pool operations."""

    def __init__(self, pool: asyncpg.Pool):
        self._pool = pool

    async def _fetchrow(self, query: str, *args: Any) -> asyncpg.Record | None:
        async with self._pool.acquire() as conn:
            return await conn.fetchrow(query, *args)

    async def _fetch(self, query: str, *args: Any) -> Iterable[asyncpg.Record]:
        async with self._pool.acquire() as conn:
            return await conn.fetch(query, *args)

    async def _execute(self, query: str, *args: Any) -> str:
        async with self._pool.acquire() as conn:
            return await conn.execute(query, *args)

