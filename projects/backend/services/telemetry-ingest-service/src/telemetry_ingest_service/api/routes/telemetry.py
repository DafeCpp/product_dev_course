"""HTTP adapters for telemetry endpoints."""
from __future__ import annotations

import asyncio
import json
import re
import time
from datetime import datetime, timezone
from uuid import UUID

import structlog
from aiohttp import web
from pydantic import ValidationError

from backend_common.aiohttp_app import extract_bearer_token
from backend_common.api.parsers import parse_bool, parse_int, parse_rfc3339, parse_uuid
from backend_common.core.exceptions import ForbiddenError
from telemetry_ingest_service.api.utils import read_json
from telemetry_ingest_service.core.exceptions import (
    AuthServiceError,
    NotFoundError,
    ScopeMismatchError,
    UnauthorizedError,
)
from telemetry_ingest_service.domain.dto import TelemetryIngestDTO
from telemetry_ingest_service.middleware.rate_limit_config import RATE_LIMIT_CONFIG
from telemetry_ingest_service.middleware.rest_rate_limit import IngestRateLimiter
from telemetry_ingest_service.prometheus_metrics import (
    INGEST_RATE_LIMITED,
    SSE_CONNECTIONS_ACTIVE,
    TELEMETRY_READINGS_INGESTED,
)
from telemetry_ingest_service.services.dependencies import (
    get_error_log_service,
    get_ingest_service,
    get_read_service,
    get_user_token_authorizer,
)
from telemetry_ingest_service.services.auth import normalize_bearer
from telemetry_ingest_service.settings import settings

logger = structlog.get_logger(__name__)
routes = web.RouteTableDef()
_rest_limiter = IngestRateLimiter(RATE_LIMIT_CONFIG)
_B64URL_RE = re.compile(r"^[A-Za-z0-9_-]+={0,2}$")


def _extract_stream_token(request: web.Request) -> str:
    for value in (
        request.headers.get("Authorization"),
        request.headers.get("X-Access-Token") or request.headers.get("X-Sensor-Token"),
        request.rel_url.query.get("access_token") or request.rel_url.query.get("token"),
    ):
        token = normalize_bearer(value)
        if token:
            return token
    raise web.HTTPUnauthorized(reason="Authorization token is required")


def _looks_like_jwt(token: str) -> bool:
    parts = token.split(".")
    return len(parts) == 3 and all(_B64URL_RE.match(part) for part in parts)


def _parse_since_ts(value: str | None) -> datetime:
    result = parse_rfc3339(value, default=datetime.fromtimestamp(0, tz=timezone.utc))
    assert result is not None
    return result


async def _authorize_user(request: web.Request, token: str, project_id: UUID) -> None:
    authorizer = await get_user_token_authorizer(request)
    try:
        await authorizer.authorize(token=token, project_id=project_id)
    except UnauthorizedError as exc:
        raise web.HTTPUnauthorized(text="Unauthorized") from exc
    except ForbiddenError as exc:
        raise web.HTTPForbidden(text="Forbidden") from exc
    except NotFoundError as exc:
        raise web.HTTPNotFound(text="Project not found") from exc
    except AuthServiceError as exc:
        raise web.HTTPBadGateway(text="Auth service error") from exc


@routes.post("/api/v1/telemetry")
async def ingest_telemetry(request: web.Request) -> web.Response:
    token = extract_bearer_token(request)
    try:
        dto = TelemetryIngestDTO.model_validate(await read_json(request))
    except ValidationError as exc:
        raise web.HTTPBadRequest(text=exc.json()) from exc

    sensor_id, readings_count = str(dto.sensor_id), len(dto.readings)
    error_log = await get_error_log_service(request)
    allowed, retry_after = _rest_limiter.check(dto.sensor_id, readings_count)
    if not allowed:
        INGEST_RATE_LIMITED.labels(transport="rest").inc()
        error_log.record_async(
            sensor_id, "rate_limited", error_message=f"Rate limit exceeded. Retry in {retry_after}s.",
            endpoint="rest", readings_count=readings_count,
        )
        raise web.HTTPTooManyRequests(
            text=f"Rate limit exceeded. Retry in {retry_after}s.",
            headers={"Retry-After": str(retry_after), "X-RateLimit-Limit": str(_rest_limiter.max_requests)},
        )

    try:
        accepted = await (await get_ingest_service(request)).ingest(dto, token=token)
    except UnauthorizedError as exc:
        error_log.record_async(sensor_id, "unauthorized", error_message=str(exc), endpoint="rest", readings_count=readings_count)
        raise web.HTTPUnauthorized(text="Unauthorized") from exc
    except ScopeMismatchError as exc:
        error_log.record_async(sensor_id, "scope_mismatch", error_message=str(exc), endpoint="rest", readings_count=readings_count)
        raise web.HTTPBadRequest(text="Scope mismatch") from exc
    except NotFoundError as exc:
        error_log.record_async(sensor_id, "not_found", error_message=str(exc), endpoint="rest", readings_count=readings_count)
        raise web.HTTPNotFound(text="Resource not found") from exc

    TELEMETRY_READINGS_INGESTED.labels(transport="rest").inc(accepted)
    return web.json_response({"status": "accepted", "accepted": accepted}, status=202)


@routes.get("/api/v1/telemetry/stream")
async def telemetry_stream(request: web.Request) -> web.StreamResponse:
    token = _extract_stream_token(request)
    sensor_id_raw = request.rel_url.query.get("sensor_id")
    if not sensor_id_raw:
        raise web.HTTPBadRequest(text="sensor_id is required")
    sensor_id = parse_uuid(sensor_id_raw, "sensor_id")
    since_ts = _parse_since_ts(request.rel_url.query.get("since_ts"))
    since_id = parse_int(request.rel_url.query.get("since_id"), default=0)
    max_events_raw = request.rel_url.query.get("max_events")
    max_events = parse_int(max_events_raw, default=0) if max_events_raw else None
    idle_timeout = float(request.rel_url.query.get("idle_timeout_seconds", "30"))

    is_user_token = _looks_like_jwt(token)
    read_service = await get_read_service(request)
    try:
        project_id = await read_service.authorize_sensor(sensor_id, token, is_user_token)
    except UnauthorizedError as exc:
        raise web.HTTPUnauthorized(text="Invalid sensor credentials") from exc
    except NotFoundError as exc:
        raise web.HTTPNotFound(text="Sensor not found") from exc
    if is_user_token:
        await _authorize_user(request, token, project_id)

    response = web.StreamResponse(status=200, headers={
        "Content-Type": "text/event-stream", "Cache-Control": "no-cache", "Connection": "keep-alive",
    })
    await response.prepare(request)
    sent, cursor_ts, cursor_id = 0, since_ts, since_id
    last_activity = last_heartbeat = time.monotonic()
    SSE_CONNECTIONS_ACTIVE.inc()
    try:
        while True:
            if request.transport is None or request.transport.is_closing():
                break
            if time.monotonic() - last_heartbeat >= settings.telemetry_stream_heartbeat_seconds:
                await response.write(b": heartbeat\n\n")
                last_heartbeat = time.monotonic()
            rows = await read_service.stream_records(project_id, sensor_id, cursor_ts, cursor_id)
            for payload in rows:
                cursor_id = payload["id"]
                cursor_ts = _parse_since_ts(payload["timestamp"])
                data = json.dumps(payload, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
                await response.write(b"event: telemetry\n")
                await response.write(b"data: " + data + b"\n\n")
                sent += 1
                last_activity = time.monotonic()
                if max_events is not None and max_events > 0 and sent >= max_events:
                    return response
            if time.monotonic() - last_activity >= idle_timeout:
                return response
            await asyncio.sleep(settings.telemetry_stream_poll_interval_seconds)
    except asyncio.CancelledError:
        raise
    except Exception:
        logger.exception("SSE telemetry stream failed")
        await response.write(b"event: error\n\ndata: stream error\n\n")
        return response
    finally:
        SSE_CONNECTIONS_ACTIVE.dec()
    return response


def _query_sensor_ids(request: web.Request) -> list[UUID]:
    sensor_ids = [parse_uuid(value, "sensor_id") for value in request.rel_url.query.getall("sensor_id", []) if value]
    if len(sensor_ids) > settings.telemetry_query_max_sensors:
        raise web.HTTPBadRequest(text=f"Too many sensor_id values (max {settings.telemetry_query_max_sensors})")
    return sensor_ids


def _require_user_token(request: web.Request) -> str:
    token = _extract_stream_token(request)
    if not _looks_like_jwt(token):
        raise web.HTTPUnauthorized(text="User token is required")
    return token


@routes.get("/api/v1/telemetry/query")
async def telemetry_query(request: web.Request) -> web.Response:
    token = _require_user_token(request)
    raw_capture_session_id = request.rel_url.query.get("capture_session_id")
    if not raw_capture_session_id:
        raise web.HTTPBadRequest(text="capture_session_id is required")
    capture_session_id, sensor_ids = parse_uuid(raw_capture_session_id, "capture_session_id"), _query_sensor_ids(request)
    since_id = parse_int(request.rel_url.query.get("since_id"), default=0)
    if since_id < 0:
        raise web.HTTPBadRequest(text="since_id must be >= 0")
    limit = parse_int(request.rel_url.query.get("limit"), default=settings.telemetry_query_default_limit)
    if limit < 1:
        raise web.HTTPBadRequest(text="limit must be >= 1")
    limit = min(limit, settings.telemetry_query_max_limit)
    include_late = parse_bool(request.rel_url.query.get("include_late"), default=True)
    order = (request.rel_url.query.get("order") or "asc").lower()
    if order not in ("asc", "desc"):
        raise web.HTTPBadRequest(text="Only order=asc or order=desc is supported")

    read_service = await get_read_service(request)
    try:
        project_id = await read_service.capture_session_project(capture_session_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text="Capture session not found") from exc
    await _authorize_user(request, token, project_id)
    points, next_since_id = await read_service.query_records(capture_session_id, sensor_ids, since_id, limit, include_late, order)
    return web.json_response({"points": points, "next_since_id": next_since_id})


@routes.get("/api/v1/telemetry/aggregated")
async def telemetry_aggregated(request: web.Request) -> web.Response:
    token = _require_user_token(request)
    raw_capture_session_id = request.rel_url.query.get("capture_session_id")
    if not raw_capture_session_id:
        raise web.HTTPBadRequest(text="capture_session_id is required")
    capture_session_id, sensor_ids = parse_uuid(raw_capture_session_id, "capture_session_id"), _query_sensor_ids(request)
    time_from_raw, time_to_raw = request.rel_url.query.get("time_from"), request.rel_url.query.get("time_to")
    time_from = _parse_since_ts(time_from_raw) if time_from_raw else None
    time_to = _parse_since_ts(time_to_raw) if time_to_raw else None
    limit = parse_int(request.rel_url.query.get("limit"), default=5000)
    if limit < 1:
        raise web.HTTPBadRequest(text="limit must be >= 1")
    order = (request.rel_url.query.get("order") or "asc").lower()
    if order not in ("asc", "desc"):
        raise web.HTTPBadRequest(text="Only order=asc or order=desc is supported")

    read_service = await get_read_service(request)
    try:
        project_id = await read_service.capture_session_project(capture_session_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text="Capture session not found") from exc
    await _authorize_user(request, token, project_id)
    buckets = await read_service.query_aggregated(
        capture_session_id, sensor_ids, request.rel_url.query.get("signal"), time_from, time_to,
        min(limit, settings.telemetry_query_max_limit), order,
    )
    return web.json_response({"buckets": buckets, "bucket_interval": "1m"})


@routes.get("/api/v1/sensors/{sensor_id}/error-log")
async def sensor_error_log(request: web.Request) -> web.Response:
    token = _require_user_token(request)
    sensor_id = parse_uuid(request.match_info["sensor_id"], "sensor_id")
    limit, offset = parse_int(request.rel_url.query.get("limit"), default=50), parse_int(request.rel_url.query.get("offset"), default=0)
    if limit < 1:
        raise web.HTTPBadRequest(text="limit must be >= 1")
    if offset < 0:
        raise web.HTTPBadRequest(text="offset must be >= 0")
    read_service = await get_read_service(request)
    try:
        project_id = await read_service.authorize_sensor(sensor_id, token, True)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text="Sensor not found") from exc
    await _authorize_user(request, token, project_id)
    entries, total = await read_service.sensor_error_entries(sensor_id, min(limit, 200), offset)
    return web.json_response({
        "sensor_id": str(sensor_id),
        "entries": [{
            "id": entry.id, "sensor_id": entry.sensor_id,
            "occurred_at": entry.occurred_at.astimezone(timezone.utc).isoformat().replace("+00:00", "Z"),
            "error_code": entry.error_code, "error_message": entry.error_message, "endpoint": entry.endpoint,
            "readings_count": entry.readings_count, "meta": entry.meta,
        } for entry in entries],
        "total": total, "limit": min(limit, 200), "offset": offset,
    })
