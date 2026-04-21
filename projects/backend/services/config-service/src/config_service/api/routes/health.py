"""Health check endpoint."""
from __future__ import annotations

from aiohttp import web

from backend_common.db.pool import get_pool

health_routes = web.RouteTableDef()


@health_routes.get("/health")
async def health(request: web.Request) -> web.Response:
    try:
        pool = await get_pool()
        async with pool.acquire() as conn:
            await conn.fetchval("SELECT 1")
        db_status = "ok"
    except Exception:
        db_status = "error"

    status = "ok" if db_status == "ok" else "degraded"
    return web.json_response({"status": status, "db": db_status})
