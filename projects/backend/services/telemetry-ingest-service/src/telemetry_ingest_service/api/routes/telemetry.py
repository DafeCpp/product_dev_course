"""Telemetry ingest endpoints."""
from __future__ import annotations

from aiohttp import web
from pydantic import ValidationError

from telemetry_ingest_service.api.utils import read_json
from telemetry_ingest_service.core.exceptions import NotFoundError, ScopeMismatchError, UnauthorizedError
from telemetry_ingest_service.domain.dto import TelemetryIngestDTO
from telemetry_ingest_service.services.telemetry import TelemetryIngestService

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
async def ingest_telemetry(request: web.Request) -> web.Response:
    """Public REST ingest endpoint for sensor telemetry."""
    token = _extract_sensor_token(request)
    body = await read_json(request)
    try:
        dto = TelemetryIngestDTO.model_validate(body)
    except ValidationError as exc:
        raise web.HTTPBadRequest(text=exc.json()) from exc

    service = TelemetryIngestService()
    try:
        accepted = await service.ingest(dto, token=token)
    except UnauthorizedError as exc:
        raise web.HTTPUnauthorized(text=str(exc)) from exc
    except ScopeMismatchError as exc:
        raise web.HTTPBadRequest(text=str(exc)) from exc
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc

    return web.json_response({"status": "accepted", "accepted": accepted}, status=202)

