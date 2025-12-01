"""Sensor management endpoints skeleton."""
from __future__ import annotations

from aiohttp import web

routes = web.RouteTableDef()


@routes.post("/api/v1/sensors")
async def register_sensor(request: web.Request):
    """Register sensor with conversion profile and token issuance."""
    raise web.HTTPNotImplemented(reason="Not implemented")


@routes.get("/api/v1/sensors")
async def list_sensors(request: web.Request):
    """List sensors with heartbeat info."""
    raise web.HTTPNotImplemented(reason="Not implemented")


@routes.post("/api/v1/sensors/{sensor_id}/rotate-token")
async def rotate_sensor_token(request: web.Request):
    """Rotate sensor token with MFA (per spec)."""
    raise web.HTTPNotImplemented(reason="Not implemented")


