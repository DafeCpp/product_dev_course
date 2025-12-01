"""Telemetry ingest and live streaming endpoints skeleton."""
from __future__ import annotations

from aiohttp import web

routes = web.RouteTableDef()


@routes.post("/api/v1/telemetry")
async def ingest_telemetry(request: web.Request):
    """Public ingest endpoint for sensor data (REST)."""
    raise web.HTTPNotImplemented(reason="Not implemented")


@routes.get("/api/v1/telemetry/stream")
async def telemetry_stream(request: web.Request):
    """WebSocket/SSE stub for real-time telemetry."""
    ws = web.WebSocketResponse()
    await ws.prepare(request)
    await ws.close(code=1011, message=b"Streaming not implemented")
    return ws


