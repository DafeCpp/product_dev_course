"""Detailed health check endpoint for telemetry-ingest-service.

Returns:
  200  when the DB is reachable (status = "ok")
  503  when the DB ping fails (status = "degraded")

Response body:
  {
    "status": "ok" | "degraded",
    "service": "<app_name>",
    "env": "<env>",
    "uptime_seconds": 123.4,
    "db": {"status": "ok"},
    "spool": {"enabled": true, "depth": 0}
  }
"""
from __future__ import annotations

import asyncio
import time

import structlog
from aiohttp import web

from backend_common.db.pool import get_pool_service as get_pool
from telemetry_ingest_service.services.spool import list_spool_files
from telemetry_ingest_service.settings import settings

logger = structlog.get_logger(__name__)

health_routes = web.RouteTableDef()

_start_time = time.monotonic()


@health_routes.get("/health")
async def health_check(_request: web.Request) -> web.Response:
    # --- DB ping ---
    db_ok = True
    db_error: str | None = None
    try:
        pool = await get_pool()
        async with pool.acquire() as conn:
            await conn.fetchval("SELECT 1")
    except Exception as exc:
        db_ok = False
        db_error = str(exc)
        logger.warning("health_db_ping_failed", error=str(exc))

    # --- spool depth (file I/O off the event loop) ---
    spool_depth = 0
    try:
        files = await asyncio.to_thread(list_spool_files)
        spool_depth = len(files)
    except Exception:
        pass

    uptime = round(time.monotonic() - _start_time, 1)

    db_info: dict = {"status": "ok" if db_ok else "error"}
    if db_error:
        db_info["error"] = db_error

    body = {
        "status": "ok" if db_ok else "degraded",
        "service": settings.app_name,
        "env": settings.env,
        "uptime_seconds": uptime,
        "db": db_info,
        "spool": {
            "enabled": settings.spool_enabled,
            "depth": spool_depth,
        },
    }

    return web.json_response(body, status=200 if db_ok else 503)
