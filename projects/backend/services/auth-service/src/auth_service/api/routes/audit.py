"""Audit log API routes."""
from __future__ import annotations

from datetime import datetime
from uuid import UUID

import structlog
from aiohttp import web

from auth_service.api.utils import get_requester_id
from auth_service.core.exceptions import ForbiddenError, InvalidCredentialsError
from auth_service.domain.dto import AuditLogEntry
from auth_service.repositories.audit import AuditRepository
from auth_service.services.dependencies import get_permission_service
from backend_common.db.pool import get_pool_service as get_pool

logger = structlog.get_logger(__name__)


def _parse_optional_uuid(value: str | None) -> UUID | None:
    if not value:
        return None
    try:
        return UUID(value)
    except ValueError:
        raise web.HTTPBadRequest(reason=f"Invalid UUID: {value}")


def _parse_optional_datetime(value: str | None, field: str) -> datetime | None:
    if not value:
        return None
    try:
        return datetime.fromisoformat(value)
    except ValueError:
        raise web.HTTPBadRequest(reason=f"Invalid datetime for '{field}': {value}")


async def list_audit_log(request: web.Request) -> web.Response:
    """Query audit log.

    Requires permission: audit.read

    Query params:
        actor_id (UUID, optional)
        action (str, optional)
        scope_type (str, optional)
        scope_id (UUID, optional)
        target_type (str, optional)
        target_id (str, optional)
        from (ISO 8601 datetime, optional)
        to (ISO 8601 datetime, optional)
        limit (int, 1-500, default 50)
        offset (int, default 0)
    """
    try:
        perm_svc = await get_permission_service(request)
        requester_id = await get_requester_id(request, perm_svc)
        await perm_svc.ensure_permission(requester_id, "audit.read")

        q = request.query
        actor_id = _parse_optional_uuid(q.get("actor_id"))
        action = q.get("action") or None
        scope_type = q.get("scope_type") or None
        scope_id = _parse_optional_uuid(q.get("scope_id"))
        target_type = q.get("target_type") or None
        target_id = q.get("target_id") or None
        from_date = _parse_optional_datetime(q.get("from"), "from")
        to_date = _parse_optional_datetime(q.get("to"), "to")

        try:
            limit = min(max(int(q.get("limit", 50)), 1), 500)
            offset = max(int(q.get("offset", 0)), 0)
        except ValueError:
            raise web.HTTPBadRequest(reason="limit and offset must be integers")

        pool = await get_pool()
        audit_repo = AuditRepository(pool)
        entries = await audit_repo.query(
            actor_id=actor_id,
            action=action,
            scope_type=scope_type,
            scope_id=scope_id,
            target_type=target_type,
            target_id=target_id,
            from_date=from_date,
            to_date=to_date,
            limit=limit,
            offset=offset,
        )

        response = [AuditLogEntry.from_model(e).model_dump() for e in entries]
        return web.json_response(response)

    except web.HTTPBadRequest:
        raise
    except InvalidCredentialsError as e:
        return web.json_response({"error": str(e)}, status=401)
    except ForbiddenError as e:
        return web.json_response({"error": str(e)}, status=403)
    except Exception as e:
        logger.error("Failed to query audit log", exc_info=e)
        return web.json_response({"error": str(e)}, status=500)


async def ingest_audit_entry(request: web.Request) -> web.Response:
    """Internal endpoint: write a single audit entry from other services.

    No authentication required — must only be accessible within the internal network.
    """
    try:
        data = await request.json()
    except Exception:
        raise web.HTTPBadRequest(reason="Invalid JSON")

    required = {"actor_id", "action", "scope_type"}
    missing = required - data.keys()
    if missing:
        raise web.HTTPBadRequest(reason=f"Missing fields: {', '.join(sorted(missing))}")

    try:
        actor_id = UUID(data["actor_id"])
    except (ValueError, KeyError):
        raise web.HTTPBadRequest(reason="Invalid actor_id")

    scope_id_str = data.get("scope_id")
    scope_id: UUID | None = None
    if scope_id_str:
        try:
            scope_id = UUID(scope_id_str)
        except ValueError:
            raise web.HTTPBadRequest(reason="Invalid scope_id")

    pool = await get_pool()
    audit_repo = AuditRepository(pool)
    entry = await audit_repo.log(
        actor_id=actor_id,
        action=data["action"],
        scope_type=data["scope_type"],
        scope_id=scope_id,
        target_type=data.get("target_type"),
        target_id=data.get("target_id"),
        details=data.get("details"),
        ip_address=data.get("ip_address"),
        user_agent=data.get("user_agent"),
    )
    return web.json_response(AuditLogEntry.from_model(entry).model_dump(), status=201)


def setup_routes(app: web.Application) -> None:
    """Setup audit routes."""
    app.router.add_get("/api/v1/audit-log", list_audit_log, name="list_audit_log")
    app.router.add_post("/api/v1/internal/audit", ingest_audit_entry, name="ingest_audit_entry")
