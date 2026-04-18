"""Health check endpoint."""
from __future__ import annotations

import time

import structlog
from aiohttp import web

from backend_common.db.pool import get_pool_service as get_pool
from script_service.settings import settings

logger = structlog.get_logger(__name__)

health_routes = web.RouteTableDef()

_start_time = time.monotonic()


@health_routes.get("/health")
async def health_check(_request: web.Request) -> web.Response:
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
    }

    return web.json_response(body, status=200 if db_ok else 503)
