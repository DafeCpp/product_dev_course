"""Audit middleware for experiment-service.

Maps mutating HTTP routes to audit actions and sends them to auth-service.
"""
from __future__ import annotations

from uuid import UUID

import structlog
from aiohttp import web
from aiohttp.typedefs import Handler, Middleware
from aiohttp.web import middleware
from aiohttp.web_response import StreamResponse

from experiment_service.services.audit_client import AuditClient

logger = structlog.get_logger(__name__)

_AUDIT_CLIENT_KEY = "audit_client"

# (METHOD, resource_canonical) → (target_type, action)
_AUDIT_MAP: dict[tuple[str, str], tuple[str, str]] = {
    ("POST",   "/api/v1/experiments"):                                         ("experiment",       "experiment.create"),
    ("PATCH",  "/api/v1/experiments/{experiment_id}"):                         ("experiment",       "experiment.update"),
    ("POST",   "/api/v1/experiments/{experiment_id}/archive"):                 ("experiment",       "experiment.archive"),
    ("DELETE", "/api/v1/experiments/{experiment_id}"):                         ("experiment",       "experiment.delete"),
    ("POST",   "/api/v1/experiments/{experiment_id}/runs"):                    ("run",              "run.create"),
    ("PATCH",  "/api/v1/runs/{run_id}"):                                       ("run",              "run.update"),
    ("DELETE", "/api/v1/runs/{run_id}"):                                       ("run",              "run.delete"),
    ("POST",   "/api/v1/sensors"):                                             ("sensor",           "sensor.create"),
    ("PATCH",  "/api/v1/sensors/{sensor_id}"):                                 ("sensor",           "sensor.update"),
    ("DELETE",  "/api/v1/sensors/{sensor_id}"):                                 ("sensor",           "sensor.delete"),
    ("POST",   "/api/v1/runs/{run_id}/capture-sessions"):                      ("capture_session",  "capture_session.create"),
    ("POST",   "/api/v1/runs/{run_id}/capture-sessions/{session_id}/stop"):    ("capture_session",  "capture_session.stop"),
    ("DELETE", "/api/v1/runs/{run_id}/capture-sessions/{session_id}"):         ("capture_session",  "capture_session.delete"),
    ("POST",   "/api/v1/webhooks"):                                            ("webhook",          "webhook.create"),
    ("DELETE", "/api/v1/webhooks/{webhook_id}"):                               ("webhook",          "webhook.delete"),
}

# URL params to use as target_id for each target_type
_TARGET_ID_PARAMS: dict[str, str] = {
    "experiment":      "experiment_id",
    "run":             "run_id",
    "sensor":          "sensor_id",
    "capture_session": "session_id",
    "webhook":         "webhook_id",
}


def _get_resource_canonical(request: web.Request) -> str | None:
    """Return the URL template for the matched route, e.g. /api/v1/experiments/{experiment_id}."""
    try:
        resource = request.match_info.route.resource
        if resource is not None:
            return resource.canonical
    except Exception:
        pass
    return None


def _extract_client_ip(request: web.Request) -> str | None:
    forwarded = request.headers.get("X-Forwarded-For")
    if forwarded:
        return forwarded.split(",")[0].strip()
    return request.remote or None


@middleware
async def audit_middleware(  # type: ignore[misc]
    request: web.Request,
    handler: Handler,
) -> StreamResponse:
    response = await handler(request)

    # Only audit successful mutating requests
    if request.method not in ("POST", "PATCH", "PUT", "DELETE"):
        return response
    if response.status >= 400:
        return response

    canonical = _get_resource_canonical(request)
    if canonical is None:
        return response

    entry = _AUDIT_MAP.get((request.method, canonical))
    if entry is None:
        return response

    target_type, action = entry

    client: AuditClient | None = request.app.get(_AUDIT_CLIENT_KEY)
    if client is None:
        return response

    # Extract user context from headers
    user_id_str = request.headers.get("X-User-Id")
    if not user_id_str:
        return response
    try:
        actor_id = UUID(user_id_str)
    except ValueError:
        return response

    # Project scope
    project_id_str = request.headers.get("X-Project-Id")
    scope_id: UUID | None = None
    if project_id_str:
        try:
            scope_id = UUID(project_id_str)
        except ValueError:
            pass

    # Target entity ID from URL params
    id_param = _TARGET_ID_PARAMS.get(target_type)
    target_id: str | None = None
    if id_param:
        target_id = request.match_info.get(id_param)

    client.log_action(
        actor_id=actor_id,
        action=action,
        scope_type="project" if scope_id else "system",
        scope_id=scope_id,
        target_type=target_type,
        target_id=target_id,
        ip_address=_extract_client_ip(request),
        user_agent=request.headers.get("User-Agent"),
    )

    return response
