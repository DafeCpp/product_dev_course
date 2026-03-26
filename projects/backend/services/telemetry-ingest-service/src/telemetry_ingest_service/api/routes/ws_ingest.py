"""WebSocket telemetry ingest endpoint."""
from __future__ import annotations

import json
import structlog
from uuid import UUID

import aiohttp
from aiohttp import web
from pydantic import ValidationError

from backend_common.db.pool import get_pool_service as get_pool

from telemetry_ingest_service.api.routes.telemetry import _normalize_bearer
from telemetry_ingest_service.core.exceptions import (
    NotFoundError,
    ScopeMismatchError,
    UnauthorizedError,
)
from telemetry_ingest_service.domain.dto import TelemetryIngestDTO, WsIngestMessageDTO
from telemetry_ingest_service.middleware.ws_rate_limit import WsRateLimiter
from telemetry_ingest_service.prometheus_metrics import (
    INGEST_RATE_LIMITED,
    TELEMETRY_READINGS_INGESTED,
    WS_CONNECTIONS_ACTIVE,
)
from telemetry_ingest_service.services.telemetry import TelemetryIngestService, hash_sensor_token
from telemetry_ingest_service.settings import settings

logger = structlog.get_logger(__name__)

ws_routes = web.RouteTableDef()

_ws_limiter = WsRateLimiter(
    max_messages=settings.ws_rate_limit_messages_per_window,
    max_readings=settings.ws_rate_limit_readings_per_window,
    window_seconds=settings.ws_rate_limit_window_seconds,
)


def _extract_ws_token(request: web.Request) -> str | None:
    token = _normalize_bearer(request.headers.get("Authorization"))
    if token:
        return token
    return _normalize_bearer(
        request.rel_url.query.get("token") or request.rel_url.query.get("access_token")
    )


@ws_routes.get("/api/v1/telemetry/ws")
async def ws_ingest(request: web.Request) -> web.WebSocketResponse:
    """WebSocket endpoint for high-frequency sensor telemetry ingest.

    The sensor authenticates once at connection time; subsequent messages
    are processed without re-authenticating the token (the connection itself
    carries the auth context).

    Connection query params:
      - sensor_id (required): UUID of the sensor
      - token / access_token (optional): sensor token when the Authorization
        header cannot be set (e.g. browser WebSocket API)

    Each message (client → server, JSON text frame):
      {
        "run_id": "<uuid>",            // optional
        "capture_session_id": "<uuid>",// optional
        "meta": {},                    // optional batch-level meta
        "readings": [
          {"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0, "meta": {}}
        ],
        "seq": 42                      // optional, echoed in ack
      }

    Acknowledgement (server → client):
      {"status": "accepted", "accepted": N, "seq": 42}

    Error (server → client):
      {"status": "error", "code": "<code>", "message": "<details>"}

    On fatal errors (bad credentials, internal failure) the server closes the
    WebSocket with an appropriate close code.  Recoverable per-message errors
    (invalid JSON, validation failure, scope mismatch) leave the connection
    open so the client can retry.
    """
    # --- parse sensor_id from query ---
    sensor_id_raw = request.rel_url.query.get("sensor_id")
    if not sensor_id_raw:
        raise web.HTTPBadRequest(text="sensor_id is required")
    try:
        sensor_id = UUID(sensor_id_raw)
    except ValueError as exc:
        raise web.HTTPBadRequest(text="Invalid sensor_id") from exc

    # --- extract token ---
    token = _extract_ws_token(request)
    if not token:
        raise web.HTTPUnauthorized(text="Authorization token is required")

    # --- authenticate sensor BEFORE upgrading to WebSocket ---
    # Raising an HTTP exception here causes the server to return a plain
    # HTTP 401/400 response, which well-behaved WS clients handle gracefully.
    pool = await get_pool()
    token_hash = hash_sensor_token(token)
    async with pool.acquire() as conn:
        row = await conn.fetchrow(
            "SELECT project_id FROM sensors WHERE id = $1 AND token_hash = $2",
            sensor_id,
            token_hash,
        )
    if row is None:
        raise web.HTTPUnauthorized(text="Invalid sensor credentials")

    # --- upgrade connection ---
    ws = web.WebSocketResponse(max_msg_size=settings.ws_max_message_bytes)
    await ws.prepare(request)

    service = TelemetryIngestService()
    log = logger.bind(sensor_id=str(sensor_id))

    WS_CONNECTIONS_ACTIVE.inc()
    try:
        async for msg in ws:
            if msg.type == aiohttp.WSMsgType.TEXT:
                await _handle_message(ws, service, sensor_id, token, msg.data, log)
            elif msg.type == aiohttp.WSMsgType.BINARY:
                await ws.send_json(
                    {"status": "error", "code": "unsupported", "message": "Binary frames are not supported; send JSON text frames"}
                )
            elif msg.type in (aiohttp.WSMsgType.ERROR, aiohttp.WSMsgType.CLOSE):
                break
    except Exception as exc:
        log.exception("ws_ingest_unhandled_error", error=str(exc))
        if not ws.closed:
            await ws.close(
                code=aiohttp.WSCloseCode.INTERNAL_ERROR,
                message=b"Internal server error",
            )
    finally:
        WS_CONNECTIONS_ACTIVE.dec()

    return ws


async def _handle_message(
    ws: web.WebSocketResponse,
    service: TelemetryIngestService,
    sensor_id: UUID,
    token: str,
    raw: str,
    log: structlog.BoundLogger,
) -> None:
    """Process a single text frame.  Sends an ack or an error JSON back."""
    # --- parse JSON ---
    try:
        data = json.loads(raw)
    except json.JSONDecodeError:
        await ws.send_json({"status": "error", "code": "invalid_json", "message": "Invalid JSON"})
        return

    # --- validate ---
    try:
        ws_msg = WsIngestMessageDTO.model_validate(data)
    except ValidationError as exc:
        await ws.send_json({"status": "error", "code": "validation_error", "message": str(exc)})
        return

    # --- rate limiting (per sensor, fixed window) ---
    limit_hit = _ws_limiter.check(sensor_id, len(ws_msg.readings))
    if limit_hit is not None:
        INGEST_RATE_LIMITED.labels(transport="ws").inc()
        await ws.send_json({
            "status": "error",
            "code": "rate_limited",
            "message": f"Rate limit exceeded ({limit_hit.reason}). Retry in {limit_hit.retry_after}s.",
            "retry_after": limit_hit.retry_after,
        })
        return

    # --- build DTO (sensor_id injected from connection context) ---
    dto = TelemetryIngestDTO(
        sensor_id=sensor_id,
        run_id=ws_msg.run_id,
        capture_session_id=ws_msg.capture_session_id,
        meta=ws_msg.meta,
        readings=ws_msg.readings,
    )

    # --- ingest ---
    try:
        accepted = await service.ingest(dto, token=token)
    except UnauthorizedError as exc:
        await ws.send_json({"status": "error", "code": "unauthorized", "message": str(exc)})
        if not ws.closed:
            await ws.close(code=aiohttp.WSCloseCode.POLICY_VIOLATION, message=b"Unauthorized")
        return
    except (ScopeMismatchError, NotFoundError) as exc:
        # Recoverable: bad scope or missing run/session — client can fix and retry.
        await ws.send_json({"status": "error", "code": "bad_request", "message": str(exc)})
        return
    except Exception as exc:
        log.exception("ws_ingest_message_error", error=str(exc))
        await ws.send_json({"status": "error", "code": "internal_error", "message": "Internal error"})
        return

    TELEMETRY_READINGS_INGESTED.labels(transport="ws").inc(accepted)
    ack: dict = {"status": "accepted", "accepted": accepted}
    if ws_msg.seq is not None:
        ack["seq"] = ws_msg.seq
    await ws.send_json(ack)
