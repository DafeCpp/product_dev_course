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

from backend_common.aiohttp_app import extract_bearer_token as _extract_bearer_token
from backend_common.db.pool import get_pool_service as get_pool

routes = web.RouteTableDef()


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


def _parse_bool(value: str | None, *, default: bool) -> bool:
    if value is None:
        return default
    normalized = value.strip().lower()
    if normalized in ("1", "true", "yes", "on"):
        return True
    if normalized in ("0", "false", "no", "off"):
        return False
    raise web.HTTPBadRequest(text="Invalid boolean query param")


def _parse_since_ts(value: str | None) -> datetime:
    """
    Parse RFC3339/ISO8601 timestamp used by SSE cursor.
    If not provided, defaults to Unix epoch (send all).
    """
    if value is None or not value.strip():
        return datetime.fromtimestamp(0, tz=timezone.utc)
    raw = value.strip()
    try:
        # Support `Z` suffix.
        if raw.endswith("Z"):
            raw = raw[:-1] + "+00:00"
        dt = datetime.fromisoformat(raw)
    except ValueError as exc:
        raise web.HTTPBadRequest(text="Invalid since_ts (expected ISO8601/RFC3339)") from exc
    if dt.tzinfo is None:
        # Treat naive timestamps as UTC to avoid accidental local-time bugs.
        dt = dt.replace(tzinfo=timezone.utc)
    return dt.astimezone(timezone.utc)


def _looks_like_jwt(token: str) -> bool:
    """Heuristic check whether a token looks like a JWT.

    JWTs have the form ``header.payload.signature`` where each part is
    base64url-encoded.  We verify the structure (3 dot-separated parts)
    and that each part only contains valid base64url characters to reduce
    false positives from arbitrary sensor tokens that happen to contain
    two dots.
    """
    parts = token.split(".")
    if len(parts) != 3:
        return False
    import re
    _b64url_re = re.compile(r"^[A-Za-z0-9_-]+={0,2}$")
    return all(_b64url_re.match(part) for part in parts)


def _serialize_telemetry_record(row: dict) -> dict:
    """Serialize a telemetry DB row into a JSON-safe dictionary.

    This helper is shared between the SSE stream and the query endpoint
    to avoid duplicating the same serialization logic.
    """
    meta = row["meta"]
    if isinstance(meta, str):
        meta = json.loads(meta)

    ts = row["timestamp"]
    if isinstance(ts, datetime):
        ts_str = ts.astimezone(timezone.utc).isoformat().replace("+00:00", "Z")
    else:
        ts_str = str(ts)

    return {
        "id": int(row["id"]),
        "project_id": str(row["project_id"]) if "project_id" in row else None,
        "sensor_id": str(row["sensor_id"]) if "sensor_id" in row else None,
        "timestamp": ts_str,
        "raw_value": row["raw_value"],
        "physical_value": row["physical_value"],
        "run_id": str(row["run_id"]) if row.get("run_id") else None,
        "capture_session_id": str(row["capture_session_id"]) if row.get("capture_session_id") else None,
        "meta": meta,
    }


async def _authorize_user_token(*, token: str, project_id: UUID) -> None:
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
      - since_ts (optional, default epoch): last seen telemetry_records.timestamp (ISO8601/RFC3339)
      - since_id (optional, default 0): tie-break for identical timestamps
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

    since_ts = _parse_since_ts(request.rel_url.query.get("since_ts"))
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
        await _authorize_user_token(token=token, project_id=project_id)

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
    cursor_ts = since_ts
    cursor_id = since_id

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
                    WHERE project_id = $1
                      AND sensor_id = $2
                      AND (timestamp, id) > ($3, $4)
                    ORDER BY timestamp ASC, id ASC
                    LIMIT 100
                    """,
                    project_id,
                    sensor_id,
                    cursor_ts,
                    cursor_id,
                )

            if rows:
                for r in rows:
                    cursor_id = int(r["id"])
                    if isinstance(r["timestamp"], datetime):
                        cursor_ts = r["timestamp"].astimezone(timezone.utc)
                    # Enrich row with sensor/project context for serialization
                    row_dict = dict(r)
                    row_dict.setdefault("sensor_id", sensor_id)
                    row_dict.setdefault("project_id", project_id)
                    payload = _serialize_telemetry_record(row_dict)
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


@routes.get("/api/v1/telemetry/query")
async def telemetry_query(request: web.Request) -> web.Response:
    """
    Query telemetry records for a capture session (historical).

    Query:
      - capture_session_id (required)
      - sensor_id (optional, repeatable, max 50)
      - since_id (optional, default 0)
      - limit (optional, default 2000, max 20000)
      - include_late (optional, default true)
      - order (optional, 'asc' or 'desc')
    """
    token = _extract_stream_token(request)
    if not _looks_like_jwt(token):
        raise web.HTTPUnauthorized(text="User token is required")

    capture_session_raw = request.rel_url.query.get("capture_session_id")
    if not capture_session_raw:
        raise web.HTTPBadRequest(text="capture_session_id is required")
    try:
        capture_session_id = UUID(capture_session_raw)
    except ValueError as exc:
        raise web.HTTPBadRequest(text="Invalid capture_session_id") from exc

    sensor_ids_raw = request.rel_url.query.getall("sensor_id", [])
    sensor_ids: list[UUID] = []
    for value in sensor_ids_raw:
        if not value:
            continue
        try:
            sensor_ids.append(UUID(value))
        except ValueError as exc:
            raise web.HTTPBadRequest(text="Invalid sensor_id") from exc
    if len(sensor_ids) > settings.telemetry_query_max_sensors:
        raise web.HTTPBadRequest(
            text=f"Too many sensor_id values (max {settings.telemetry_query_max_sensors})"
        )

    since_id = _parse_int(request.rel_url.query.get("since_id"), default=0)
    if since_id < 0:
        raise web.HTTPBadRequest(text="since_id must be >= 0")
    limit = _parse_int(request.rel_url.query.get("limit"), default=settings.telemetry_query_default_limit)
    if limit < 1:
        raise web.HTTPBadRequest(text="limit must be >= 1")
    if limit > settings.telemetry_query_max_limit:
        limit = settings.telemetry_query_max_limit
    include_late = _parse_bool(request.rel_url.query.get("include_late"), default=True)
    order = (request.rel_url.query.get("order") or "asc").lower()
    if order not in ("asc", "desc"):
        raise web.HTTPBadRequest(text="Only order=asc or order=desc is supported")

    pool = await get_pool()
    async with pool.acquire() as conn:
        row = await conn.fetchrow(
            "SELECT project_id FROM capture_sessions WHERE id = $1",
            capture_session_id,
        )
        if row is None:
            raise web.HTTPNotFound(text="Capture session not found")
        project_id = UUID(str(row["project_id"]))

    await _authorize_user_token(token=token, project_id=project_id)

    conditions: list[str] = []
    params: list[object] = []
    param_idx = 1

    if include_late:
        conditions.append(
            f"(capture_session_id = ${param_idx} OR (meta->'__system'->>'capture_session_id') = ${param_idx}::text)"
        )
    else:
        conditions.append(f"capture_session_id = ${param_idx}")
        conditions.append(
            "COALESCE((meta->'__system'->>'late')::boolean, false) = false"
        )
    params.append(capture_session_id)
    param_idx += 1

    if order == "asc":
        conditions.append(f"id > ${param_idx}")
        params.append(since_id)
        param_idx += 1
    elif since_id > 0:
        conditions.append(f"id < ${param_idx}")
        params.append(since_id)
        param_idx += 1

    if sensor_ids:
        conditions.append(f"sensor_id = ANY(${param_idx}::uuid[])")
        params.append(sensor_ids)
        param_idx += 1

    conditions_sql = " AND ".join(conditions)
    order_sql = "ASC" if order == "asc" else "DESC"
    sql = f"""
        SELECT id, project_id, sensor_id, timestamp, raw_value, physical_value,
               run_id, capture_session_id, meta
        FROM telemetry_records
        WHERE {conditions_sql}
        ORDER BY id {order_sql}
        LIMIT ${param_idx}
    """
    params.append(limit)

    async with pool.acquire() as conn:
        rows = await conn.fetch(sql, *params)

    points = [_serialize_telemetry_record(dict(r)) for r in rows]

    next_since_id = points[-1]["id"] if len(points) == limit else None
    return web.json_response({"points": points, "next_since_id": next_since_id})
