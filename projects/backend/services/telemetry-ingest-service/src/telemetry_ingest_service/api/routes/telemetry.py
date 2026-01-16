"""Telemetry ingest endpoints."""
from __future__ import annotations

import asyncio
import json
import time
from datetime import datetime, timezone
from uuid import UUID

import aiohttp
from aiohttp import web
from pydantic import ValidationError

from telemetry_ingest_service.api.utils import read_json
from telemetry_ingest_service.core.exceptions import NotFoundError, ScopeMismatchError, UnauthorizedError
from telemetry_ingest_service.domain.dto import TelemetryIngestDTO
from telemetry_ingest_service.services.telemetry import TelemetryIngestService
from telemetry_ingest_service.services.telemetry import hash_sensor_token
from telemetry_ingest_service.settings import settings

from backend_common.db.pool import get_pool_service as get_pool

routes = web.RouteTableDef()


def _extract_bearer_token(request: web.Request) -> str:
    auth_header = request.headers.get("Authorization")
    if not auth_header or not auth_header.startswith("Bearer "):
        raise web.HTTPUnauthorized(reason="Authorization token is required")
    token = auth_header[len("Bearer ") :].strip()
    if not token:
        raise web.HTTPUnauthorized(reason="Authorization token is required")
    return token


def _normalize_bearer(value: str | None) -> str | None:
    if not value:
        return None
    value = value.strip()
    if not value:
        return None
    if value.lower().startswith("bearer "):
        value = value[7:].strip()
    return value or None


def _extract_stream_token(request: web.Request) -> str:
    # Prefer standard Authorization header, then fall back to headers/query
    # for SSE clients that cannot set custom headers.
    token = _normalize_bearer(request.headers.get("Authorization"))
    if token:
        return token
    token = _normalize_bearer(
        request.headers.get("X-Access-Token") or request.headers.get("X-Sensor-Token")
    )
    if token:
        return token
    token = _normalize_bearer(
        request.rel_url.query.get("access_token") or request.rel_url.query.get("token")
    )
    if token:
        return token
    raise web.HTTPUnauthorized(reason="Authorization token is required")


@routes.post("/api/v1/telemetry")
async def ingest_telemetry(request: web.Request) -> web.Response:
    """Public REST ingest endpoint for sensor telemetry."""
    token = _extract_bearer_token(request)
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


def _parse_int(value: str | None, *, default: int) -> int:
    if value is None:
        return default
    try:
        return int(value)
    except ValueError as exc:
        raise web.HTTPBadRequest(text="Invalid integer query param") from exc


def _looks_like_jwt(token: str) -> bool:
    # JWT is "header.payload.signature" (3 dot-separated parts)
    return token.count(".") == 2


async def _authorize_stream_user(*, token: str, project_id: UUID) -> None:
    """
    Validate that the bearer token belongs to a user that is a member of the given project.
    Uses auth-service:
      - GET /auth/me
      - GET /projects/{project_id}/members
    """
    base = settings.auth_service_url.rstrip("/")
    if base.endswith("/api/v1"):
        # Support envs that include the experiment-service prefix by mistake.
        base = base[: -len("/api/v1")]
    headers = {"Authorization": f"Bearer {token}"}

    async with aiohttp.ClientSession() as session:
        async with session.get(f"{base}/auth/me", headers=headers) as resp:
            if resp.status != 200:
                raise web.HTTPUnauthorized(text="Unauthorized")
            me = await resp.json()
            user_id = me.get("id")
            if not user_id:
                raise web.HTTPUnauthorized(text="Unauthorized")

        async with session.get(f"{base}/projects/{project_id}/members", headers=headers) as resp:
            if resp.status == 403:
                raise web.HTTPForbidden(text="Forbidden")
            if resp.status == 404:
                raise web.HTTPNotFound(text="Project not found")
            if resp.status != 200:
                raise web.HTTPBadGateway(text="Auth service error")
            data = await resp.json()
            members = data.get("members") or []
            if not any(str(m.get("user_id")) == str(user_id) for m in members):
                raise web.HTTPForbidden(text="Forbidden")


@routes.get("/api/v1/telemetry/stream")
async def telemetry_stream(request: web.Request) -> web.StreamResponse:
    """
    SSE endpoint to stream telemetry records for a sensor.

    Query:
      - sensor_id (required)
      - since_id (optional, default 0): last seen telemetry_records.id
      - max_events (optional): stop after sending N events (useful for tests)
      - idle_timeout_seconds (optional, default 30): stop after N seconds without new data
    """
    token = _extract_stream_token(request)
    sensor_id_raw = request.rel_url.query.get("sensor_id")
    if not sensor_id_raw:
        raise web.HTTPBadRequest(text="sensor_id is required")
    try:
        sensor_id = UUID(sensor_id_raw)
    except ValueError as exc:
        raise web.HTTPBadRequest(text="Invalid sensor_id") from exc

    since_id = _parse_int(request.rel_url.query.get("since_id"), default=0)
    max_events_raw = request.rel_url.query.get("max_events")
    max_events = _parse_int(max_events_raw, default=0) if max_events_raw else None
    idle_timeout = float(request.rel_url.query.get("idle_timeout_seconds", "30"))

    pool = await get_pool()
    async with pool.acquire() as conn:
        if _looks_like_jwt(token):
            row = await conn.fetchrow("SELECT project_id FROM sensors WHERE id = $1", sensor_id)
            if row is None:
                raise web.HTTPNotFound(text="Sensor not found")
            project_id = row["project_id"]
        else:
            token_hash = hash_sensor_token(token)
            row = await conn.fetchrow(
                "SELECT project_id FROM sensors WHERE id = $1 AND token_hash = $2",
                sensor_id,
                token_hash,
            )
            if row is None:
                raise web.HTTPUnauthorized(text="Invalid sensor credentials")
            project_id = row["project_id"]

    # If token is a user token, enforce membership in the sensor's project.
    if _looks_like_jwt(token):
        await _authorize_stream_user(token=token, project_id=project_id)

    resp = web.StreamResponse(
        status=200,
        headers={
            "Content-Type": "text/event-stream",
            "Cache-Control": "no-cache",
            "Connection": "keep-alive",
        },
    )
    await resp.prepare(request)

    sent = 0
    last_activity = time.monotonic()
    last_heartbeat = time.monotonic()
    cursor = since_id

    try:
        while True:
            if request.transport is None or request.transport.is_closing():
                break

            # heartbeat
            if (time.monotonic() - last_heartbeat) >= settings.telemetry_stream_heartbeat_seconds:
                await resp.write(b": heartbeat\n\n")
                last_heartbeat = time.monotonic()

            async with pool.acquire() as conn:
                rows = await conn.fetch(
                    """
                    SELECT id, timestamp, raw_value, physical_value, meta, run_id, capture_session_id
                    FROM telemetry_records
                    WHERE project_id = $1 AND sensor_id = $2 AND id > $3
                    ORDER BY id ASC
                    LIMIT 100
                    """,
                    project_id,
                    sensor_id,
                    cursor,
                )

            if rows:
                for r in rows:
                    cursor = int(r["id"])
                    payload = {
                        "id": cursor,
                        "sensor_id": str(sensor_id),
                        "project_id": str(project_id),
                        "timestamp": (
                            r["timestamp"].astimezone(timezone.utc).isoformat().replace("+00:00", "Z")
                            if isinstance(r["timestamp"], datetime)
                            else str(r["timestamp"])
                        ),
                        "raw_value": r["raw_value"],
                        "physical_value": r["physical_value"],
                        "run_id": str(r["run_id"]) if r["run_id"] else None,
                        "capture_session_id": str(r["capture_session_id"]) if r["capture_session_id"] else None,
                        "meta": r["meta"] if isinstance(r["meta"], dict) else json.loads(r["meta"]),
                    }
                    data = json.dumps(payload, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
                    await resp.write(b"event: telemetry\n")
                    await resp.write(b"data: " + data + b"\n\n")
                    sent += 1
                    last_activity = time.monotonic()
                    if max_events is not None and max_events > 0 and sent >= max_events:
                        return resp

            if (time.monotonic() - last_activity) >= idle_timeout:
                return resp

            await asyncio.sleep(settings.telemetry_stream_poll_interval_seconds)
    except asyncio.CancelledError:
        raise
    except Exception as exc:
        # avoid raising after headers are sent; close stream
        await resp.write(b"event: error\n")
        await resp.write(b"data: " + str(exc).encode("utf-8") + b"\n\n")
        return resp
    return resp
