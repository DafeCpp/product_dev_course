"""In-memory TTL cache for active conversion profiles.

The cache avoids hitting the database on every ingest request, which is
critical because the control loop can send data at 500 Hz.

Usage::

    from telemetry_ingest_service.services.profile_cache import profile_cache

    profile = await profile_cache.get_active_profile(conn, sensor_id)
"""
from __future__ import annotations

import json
import time
from dataclasses import dataclass
from typing import Any
from uuid import UUID

from telemetry_ingest_service.settings import settings


@dataclass(frozen=True, slots=True)
class CachedProfile:
    """Minimal projection of a conversion profile needed for ingest."""

    profile_id: UUID
    kind: str
    payload: dict[str, Any]


class ProfileCache:
    """TTL-based in-memory cache keyed by sensor_id."""

    def __init__(self, ttl_seconds: float) -> None:
        self._cache: dict[UUID, tuple[CachedProfile | None, float]] = {}
        self._ttl = ttl_seconds

    async def get_active_profile(
        self, conn, sensor_id: UUID  # noqa: ANN001 (asyncpg connection)
    ) -> CachedProfile | None:
        now = time.monotonic()
        entry = self._cache.get(sensor_id)
        if entry is not None and entry[1] > now:
            return entry[0]

        profile = await self._load_from_db(conn, sensor_id)
        self._cache[sensor_id] = (profile, now + self._ttl)
        return profile

    def invalidate(self, sensor_id: UUID) -> None:
        self._cache.pop(sensor_id, None)

    @staticmethod
    async def _load_from_db(conn, sensor_id: UUID) -> CachedProfile | None:  # noqa: ANN001
        row = await conn.fetchrow(
            """
            SELECT cp.id, cp.kind, cp.payload
            FROM sensors s
            JOIN conversion_profiles cp ON cp.id = s.active_profile_id
            WHERE s.id = $1 AND cp.status = 'active'
            """,
            sensor_id,
        )
        if row is None:
            return None
        payload = row["payload"]
        if isinstance(payload, str):
            payload = json.loads(payload)
        return CachedProfile(
            profile_id=UUID(str(row["id"])),
            kind=row["kind"],
            payload=payload,
        )


# Module-level singleton (TelemetryIngestService is created per-request).
profile_cache = ProfileCache(ttl_seconds=settings.conversion_profile_cache_ttl_seconds)
