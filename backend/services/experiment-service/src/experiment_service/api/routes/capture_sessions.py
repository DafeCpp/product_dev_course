"""Capture session endpoints skeleton."""
from __future__ import annotations

from aiohttp import web

routes = web.RouteTableDef()


@routes.get("/api/v1/runs/{run_id}/capture-sessions")
async def list_capture_sessions(request: web.Request):
    """List capture sessions for a run."""
    raise web.HTTPNotImplemented(reason="Not implemented")


@routes.post("/api/v1/runs/{run_id}/capture-sessions")
async def create_capture_session(request: web.Request):
    """Start capture session (start countdown)."""
    raise web.HTTPNotImplemented(reason="Not implemented")


@routes.post("/api/v1/runs/{run_id}/capture-sessions/{session_id}/stop")
async def stop_capture_session(request: web.Request):
    """Stop capture session."""
    raise web.HTTPNotImplemented(reason="Not implemented")


@routes.delete("/api/v1/runs/{run_id}/capture-sessions/{session_id}")
async def delete_capture_session(request: web.Request):
    """Delete capture session respecting lifecycle constraints."""
    raise web.HTTPNotImplemented(reason="Not implemented")


