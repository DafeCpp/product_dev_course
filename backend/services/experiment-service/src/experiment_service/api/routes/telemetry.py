"""Telemetry ingest and live streaming endpoints."""
from __future__ import annotations

from aiohttp import web
from pydantic import ValidationError

from experiment_service.api.utils import read_json
from experiment_service.core.exceptions import (
    NotFoundError,
    ScopeMismatchError,
    UnauthorizedError,
)
from experiment_service.domain.dto import TelemetryIngestDTO
from experiment_service.services.dependencies import get_telemetry_service

routes = web.RouteTableDef()


def _extract_sensor_token(request: web.Request) -> str:
    auth_header = request.headers.get("Authorization")
    if not auth_header or not auth_header.startswith("Bearer "):
        raise web.HTTPUnauthorized(reason="Sensor token is required")
    token = auth_header[len("Bearer ") :].strip()
    if not token:
        raise web.HTTPUnauthorized(reason="Sensor token is required")
    return token


@routes.post("/api/v1/telemetry")
async def ingest_telemetry(request: web.Request):
    """Public ingest endpoint for sensor data (REST)."""
    token = _extract_sensor_token(request)
    body = await read_json(request)
    try:
        dto = TelemetryIngestDTO.model_validate(body)
    except ValidationError as exc:
        raise web.HTTPBadRequest(text=exc.json()) from exc
    service = await get_telemetry_service(request)
    try:
        await service.ingest(dto, token=token)
    except UnauthorizedError as exc:
        raise web.HTTPUnauthorized(text=str(exc)) from exc
    except ScopeMismatchError as exc:
        raise web.HTTPBadRequest(text=str(exc)) from exc
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.json_response({"status": "accepted"}, status=202)


@routes.get("/api/v1/telemetry/stream")
async def telemetry_stream(request: web.Request):
    """WebSocket/SSE stub for real-time telemetry."""
    ws = web.WebSocketResponse()
    await ws.prepare(request)
    await ws.close(code=1011, message=b"Streaming not implemented")
    return ws
