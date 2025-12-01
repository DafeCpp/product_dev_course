"""Conversion profile endpoints skeleton."""
from __future__ import annotations

from aiohttp import web

routes = web.RouteTableDef()


@routes.post("/api/v1/sensors/{sensor_id}/conversion-profiles")
async def create_profile(request: web.Request):
    """Create new conversion profile version."""
    raise web.HTTPNotImplemented(reason="Not implemented")


@routes.get("/api/v1/sensors/{sensor_id}/conversion-profiles")
async def list_profiles(request: web.Request):
    """List profiles for a sensor."""
    raise web.HTTPNotImplemented(reason="Not implemented")


@routes.post("/api/v1/sensors/{sensor_id}/conversion-profiles/{profile_id}/publish")
async def publish_profile(request: web.Request):
    """Publish profile and trigger backfill if required."""
    raise web.HTTPNotImplemented(reason="Not implemented")


