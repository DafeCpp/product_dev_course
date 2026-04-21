"""Config CRUD endpoints."""
from __future__ import annotations

import json
from datetime import datetime, timezone
from uuid import UUID, uuid4

from aiohttp import web

from config_service.core.exceptions import (
    ConfigNotFoundError,
    ConfigValidationError,
    IdempotencyConflictError,
    VersionConflictError,
)
from config_service.domain.dto import (
    ActivateDeactivateRequest,
    ConfigCreate,
    ConfigPatch,
    ConfigResponse,
    RollbackRequest,
)
from config_service.domain.enums import ConfigType
from config_service.prometheus_metrics import config_optimistic_lock_conflicts_total
from config_service.services.dependencies import (
    get_audit_service,
    get_config_service,
    get_idempotency_service,
    get_validation_service,
    require_current_user,
    ensure_permission,
)

routes = web.RouteTableDef()

_IDEMPOTENCY_KEY_HEADER = "Idempotency-Key"


def _correlation_id(request: web.Request) -> str | None:
    return request.headers.get("X-Trace-Id") or request.headers.get("X-Correlation-Id")


def _source_ip(request: web.Request) -> str | None:
    return request.headers.get("X-Forwarded-For") or request.remote


def _extract_version(request: web.Request, body: dict[str, object]) -> tuple[int, int]:
    if_match = request.headers.get("If-Match")
    if not if_match:
        raise web.HTTPPreconditionRequired(
            reason="If-Match header and version in body are required"
        )
    body_version = body.get("version")
    if body_version is None:
        raise web.HTTPPreconditionRequired(reason="version field is required in request body")

    header_version = if_match.strip('"')
    try:
        header_version_int = int(header_version)
    except ValueError:
        raise web.HTTPBadRequest(reason="If-Match header must be a numeric version")

    try:
        body_version_int = int(str(body_version))
    except (TypeError, ValueError):
        raise web.HTTPBadRequest(reason="version in body must be an integer")
    if header_version_int != body_version_int:
        raise web.HTTPBadRequest(
            reason="version mismatch between If-Match header and request body"
        )
    return header_version_int, body_version_int


def _extract_version_from_query(request: web.Request) -> int:
    if_match = request.headers.get("If-Match")
    version_str = request.rel_url.query.get("version")
    if not if_match or not version_str:
        raise web.HTTPPreconditionRequired(
            reason="If-Match header and version query parameter are required"
        )
    try:
        header_v = int(if_match.strip('"'))
        query_v = int(version_str)
    except ValueError:
        raise web.HTTPBadRequest(reason="version must be an integer")
    if header_v != query_v:
        raise web.HTTPBadRequest(
            reason="version mismatch between If-Match header and version query param"
        )
    return header_v


def _config_to_response(config: object, redact: bool) -> dict[str, object]:
    resp: dict[str, object] = ConfigResponse.model_validate(
        config, from_attributes=True
    ).model_dump(mode="json")
    if redact:
        resp["value"] = "***"
    return resp


@routes.post("/api/v1/config")
async def create_config(request: web.Request) -> web.Response:
    user = require_current_user(request)
    ensure_permission(user, "configs.create")

    body = await request.json()
    dry_run = request.rel_url.query.get("dry_run", "false").lower() == "true"

    try:
        dto = ConfigCreate.model_validate(body)
    except Exception as exc:
        raise web.HTTPUnprocessableEntity(reason=str(exc).replace("\n", " ").replace("\r", ""))

    # Dry-run: validate schema but do NOT write to DB
    if dry_run:
        validation_svc = await get_validation_service(request)
        try:
            await validation_svc.validate_strict(dto.config_type, dto.value)
        except ConfigValidationError as exc:
            raise web.HTTPUnprocessableEntity(
                text=json.dumps({"error": "Validation failed", "details": exc.errors}),
                content_type="application/json",
            )
        now = datetime.now(tz=timezone.utc).isoformat()
        redact = dto.is_sensitive and "configs.sensitive.read" not in user.system_permissions
        preview: dict[str, object] = {
            "id": str(uuid4()),
            "service_name": dto.service_name,
            "project_id": dto.project_id,
            "key": dto.key,
            "config_type": dto.config_type.value,
            "description": dto.description,
            "value": "***" if redact else dto.value,
            "metadata": dto.metadata,
            "is_active": True,
            "is_critical": dto.is_critical,
            "is_sensitive": dto.is_sensitive,
            "version": 1,
            "created_by": user.user_id,
            "updated_by": user.user_id,
            "created_at": now,
            "updated_at": now,
            "deleted_at": None,
        }
        return web.json_response({"preview": preview, "dry_run": True}, status=200)

    svc = await get_config_service(request)
    idempotency_svc = await get_idempotency_service(request)
    audit_svc = await get_audit_service(request)
    idempotency_key = request.headers.get(_IDEMPOTENCY_KEY_HEADER)

    if idempotency_key:
        body_hash = idempotency_svc.body_hash(body)
        try:
            cached = await idempotency_svc.get_cached_response(
                idempotency_key, user.user_id, request.path, body_hash
            )
        except IdempotencyConflictError:
            raise web.HTTPConflict(reason="Idempotency key reused with different payload")
        if cached is not None:
            return idempotency_svc.build_response(cached)

    try:
        config = await svc.create(
            service_name=dto.service_name,
            project_id=dto.project_id,
            key=dto.key,
            config_type=dto.config_type,
            description=dto.description,
            value=dto.value,
            metadata=dto.metadata,
            is_critical=dto.is_critical,
            is_sensitive=dto.is_sensitive,
            created_by=user.user_id,
            change_reason=dto.change_reason,
            source_ip=_source_ip(request),
            user_agent=request.headers.get("User-Agent"),
            correlation_id=_correlation_id(request),
        )
    except ConfigValidationError as exc:
        raise web.HTTPUnprocessableEntity(
            text=json.dumps({"error": "Validation failed", "details": exc.errors}),
            content_type="application/json",
        )

    audit_svc.log(
        action="create",
        actor=user.user_id,
        service_name=config.service_name,
        config_type=config.config_type.value,
        config_id=str(config.id),
        key=config.key,
        change_reason=dto.change_reason,
        is_critical=config.is_critical,
        is_sensitive=config.is_sensitive,
        correlation_id=_correlation_id(request),
        source_ip=_source_ip(request),
        user_agent=request.headers.get("User-Agent"),
        value=config.value,
    )

    redact = config.is_sensitive and "configs.sensitive.read" not in user.system_permissions
    resp_body = _config_to_response(config, redact)

    if idempotency_key:
        body_hash = idempotency_svc.body_hash(body)
        await idempotency_svc.store_response(
            idempotency_key, user.user_id, request.path, body_hash, 201, resp_body
        )

    return web.json_response(resp_body, status=201, headers={"ETag": f'"{config.version}"'})


@routes.get("/api/v1/config")
async def list_configs(request: web.Request) -> web.Response:
    user = require_current_user(request)
    ensure_permission(user, "configs.view")

    q = request.rel_url.query
    service_name = q.get("service")
    project_id = q.get("project")
    config_type_str = q.get("config_type")
    is_active_str = q.get("is_active")
    limit = min(int(q.get("limit", "50")), 500)
    cursor = q.get("cursor")

    config_type = ConfigType(config_type_str) if config_type_str else None
    is_active = {"true": True, "false": False}.get(is_active_str or "") if is_active_str else None

    svc = await get_config_service(request)
    items, next_cursor = await svc.list_configs(
        service_name=service_name,
        project_id=project_id,
        config_type=config_type,
        is_active=is_active,
        limit=limit,
        cursor=cursor,
    )

    can_read_sensitive = user.is_superadmin or "configs.sensitive.read" in user.system_permissions
    result = [_config_to_response(c, c.is_sensitive and not can_read_sensitive) for c in items]
    return web.json_response({"items": result, "next_cursor": next_cursor})


@routes.get("/api/v1/config/{config_id}")
async def get_config(request: web.Request) -> web.Response:
    user = require_current_user(request)
    ensure_permission(user, "configs.view")

    config_id = UUID(request.match_info["config_id"])
    svc = await get_config_service(request)

    try:
        config = await svc.get(config_id)
    except ConfigNotFoundError:
        raise web.HTTPNotFound()

    can_read_sensitive = user.is_superadmin or "configs.sensitive.read" in user.system_permissions
    redact = config.is_sensitive and not can_read_sensitive
    return web.json_response(
        _config_to_response(config, redact),
        headers={"ETag": f'"{config.version}"'},
    )


@routes.patch("/api/v1/config/{config_id}")
async def patch_config(request: web.Request) -> web.Response:
    user = require_current_user(request)
    ensure_permission(user, "configs.update")

    config_id = UUID(request.match_info["config_id"])
    body = await request.json()
    dry_run = request.rel_url.query.get("dry_run", "false").lower() == "true"

    expected_version, _ = _extract_version(request, body)

    try:
        dto = ConfigPatch.model_validate(body)
    except Exception as exc:
        raise web.HTTPUnprocessableEntity(reason=str(exc).replace("\n", " ").replace("\r", ""))

    svc = await get_config_service(request)
    audit_svc = await get_audit_service(request)

    # Dry-run: validate but do NOT write to DB
    if dry_run:
        try:
            current = await svc.get(config_id)
        except ConfigNotFoundError:
            raise web.HTTPNotFound()
        if dto.value is not None:
            validation_svc = await get_validation_service(request)
            try:
                await validation_svc.validate_strict(current.config_type, dto.value)
            except ConfigValidationError as exc:
                raise web.HTTPUnprocessableEntity(
                    text=json.dumps({"error": "Validation failed", "details": exc.errors}),
                    content_type="application/json",
                )
        merged_value = dto.value if dto.value is not None else current.value
        merged_meta = dto.metadata if dto.metadata is not None else current.metadata
        merged_active = dto.is_active if dto.is_active is not None else current.is_active
        can_read_sensitive = user.is_superadmin or "configs.sensitive.read" in user.system_permissions
        redact = current.is_sensitive and not can_read_sensitive
        preview = {
            "id": str(current.id),
            "service_name": current.service_name,
            "project_id": current.project_id,
            "key": current.key,
            "config_type": current.config_type.value,
            "description": dto.description if dto.description is not None else current.description,
            "value": "***" if redact else merged_value,
            "metadata": merged_meta,
            "is_active": merged_active,
            "is_critical": dto.is_critical if dto.is_critical is not None else current.is_critical,
            "is_sensitive": dto.is_sensitive if dto.is_sensitive is not None else current.is_sensitive,
            "version": current.version + 1,
            "created_by": current.created_by,
            "updated_by": user.user_id,
            "created_at": current.created_at.isoformat(),
            "updated_at": datetime.now(tz=timezone.utc).isoformat(),
            "deleted_at": None,
        }
        return web.json_response({"preview": preview, "dry_run": True})

    try:
        config = await svc.patch(
            config_id,
            expected_version,
            changed_by=user.user_id,
            change_reason=dto.change_reason,
            source_ip=_source_ip(request),
            user_agent=request.headers.get("User-Agent"),
            correlation_id=_correlation_id(request),
            description=dto.description,
            value=dto.value,
            metadata=dto.metadata,
            is_active=dto.is_active,
            is_critical=dto.is_critical,
            is_sensitive=dto.is_sensitive,
        )
    except ConfigNotFoundError:
        raise web.HTTPNotFound()
    except VersionConflictError:
        config_optimistic_lock_conflicts_total.labels(route="PATCH /api/v1/config/{id}").inc()
        raise web.HTTPPreconditionFailed(reason="Version conflict — please re-fetch and retry")
    except ConfigValidationError as exc:
        raise web.HTTPUnprocessableEntity(
            text=json.dumps({"error": "Validation failed", "details": exc.errors}),
            content_type="application/json",
        )

    audit_svc.log(
        action="patch",
        actor=user.user_id,
        service_name=config.service_name,
        config_type=config.config_type.value,
        config_id=str(config.id),
        key=config.key,
        change_reason=dto.change_reason,
        is_critical=config.is_critical,
        is_sensitive=config.is_sensitive,
        correlation_id=_correlation_id(request),
        source_ip=_source_ip(request),
        user_agent=request.headers.get("User-Agent"),
        value=config.value,
    )

    can_read_sensitive = user.is_superadmin or "configs.sensitive.read" in user.system_permissions
    redact = config.is_sensitive and not can_read_sensitive
    resp_body = _config_to_response(config, redact)

    return web.json_response(resp_body, headers={"ETag": f'"{config.version}"'})


@routes.delete("/api/v1/config/{config_id}")
async def delete_config(request: web.Request) -> web.Response:
    user = require_current_user(request)
    ensure_permission(user, "configs.delete")

    config_id = UUID(request.match_info["config_id"])
    expected_version = _extract_version_from_query(request)
    change_reason = request.rel_url.query.get("change_reason", "").strip()
    if not change_reason:
        raise web.HTTPBadRequest(reason="change_reason query parameter is required")

    svc = await get_config_service(request)
    audit_svc = await get_audit_service(request)

    try:
        config = await svc.soft_delete(
            config_id,
            expected_version,
            deleted_by=user.user_id,
            change_reason=change_reason,
            source_ip=_source_ip(request),
            user_agent=request.headers.get("User-Agent"),
            correlation_id=_correlation_id(request),
        )
    except ConfigNotFoundError:
        raise web.HTTPNotFound()
    except VersionConflictError:
        config_optimistic_lock_conflicts_total.labels(route="DELETE /api/v1/config/{id}").inc()
        raise web.HTTPPreconditionFailed(reason="Version conflict — please re-fetch and retry")

    audit_svc.log(
        action="delete",
        actor=user.user_id,
        service_name=config.service_name,
        config_type=config.config_type.value,
        config_id=str(config.id),
        key=config.key,
        change_reason=change_reason,
        is_critical=config.is_critical,
        is_sensitive=config.is_sensitive,
        correlation_id=_correlation_id(request),
        source_ip=_source_ip(request),
        user_agent=request.headers.get("User-Agent"),
    )
    return web.Response(status=204)


@routes.post("/api/v1/config/{config_id}/activate")
async def activate_config(request: web.Request) -> web.Response:
    user = require_current_user(request)
    ensure_permission(user, "configs.activate")

    config_id = UUID(request.match_info["config_id"])
    body = await request.json()
    expected_version, _ = _extract_version(request, body)

    try:
        dto = ActivateDeactivateRequest.model_validate(body)
    except Exception as exc:
        raise web.HTTPUnprocessableEntity(reason=str(exc).replace("\n", " ").replace("\r", ""))

    svc = await get_config_service(request)
    try:
        config = await svc.patch(
            config_id,
            expected_version,
            changed_by=user.user_id,
            change_reason=dto.change_reason,
            source_ip=_source_ip(request),
            user_agent=request.headers.get("User-Agent"),
            correlation_id=_correlation_id(request),
            is_active=True,
        )
    except ConfigNotFoundError:
        raise web.HTTPNotFound()
    except VersionConflictError:
        config_optimistic_lock_conflicts_total.labels(route="POST activate").inc()
        raise web.HTTPPreconditionFailed(reason="Version conflict")

    return web.json_response(
        _config_to_response(config, False),
        headers={"ETag": f'"{config.version}"'},
    )


@routes.post("/api/v1/config/{config_id}/deactivate")
async def deactivate_config(request: web.Request) -> web.Response:
    user = require_current_user(request)
    ensure_permission(user, "configs.activate")

    config_id = UUID(request.match_info["config_id"])
    body = await request.json()
    expected_version, _ = _extract_version(request, body)

    try:
        dto = ActivateDeactivateRequest.model_validate(body)
    except Exception as exc:
        raise web.HTTPUnprocessableEntity(reason=str(exc).replace("\n", " ").replace("\r", ""))

    svc = await get_config_service(request)
    try:
        config = await svc.patch(
            config_id,
            expected_version,
            changed_by=user.user_id,
            change_reason=dto.change_reason,
            source_ip=_source_ip(request),
            user_agent=request.headers.get("User-Agent"),
            correlation_id=_correlation_id(request),
            is_active=False,
        )
    except ConfigNotFoundError:
        raise web.HTTPNotFound()
    except VersionConflictError:
        config_optimistic_lock_conflicts_total.labels(route="POST deactivate").inc()
        raise web.HTTPPreconditionFailed(reason="Version conflict")

    return web.json_response(
        _config_to_response(config, False),
        headers={"ETag": f'"{config.version}"'},
    )


@routes.post("/api/v1/config/{config_id}/rollback")
async def rollback_config(request: web.Request) -> web.Response:
    user = require_current_user(request)
    ensure_permission(user, "configs.rollback")

    config_id = UUID(request.match_info["config_id"])
    body = await request.json()
    expected_version, _ = _extract_version(request, body)

    try:
        dto = RollbackRequest.model_validate(body)
    except Exception as exc:
        raise web.HTTPUnprocessableEntity(reason=str(exc).replace("\n", " ").replace("\r", ""))

    svc = await get_config_service(request)
    audit_svc = await get_audit_service(request)

    try:
        config = await svc.rollback(
            config_id,
            expected_version,
            dto.target_version,
            changed_by=user.user_id,
            change_reason=dto.change_reason,
            source_ip=_source_ip(request),
            user_agent=request.headers.get("User-Agent"),
            correlation_id=_correlation_id(request),
        )
    except ConfigNotFoundError:
        raise web.HTTPNotFound()
    except VersionConflictError:
        config_optimistic_lock_conflicts_total.labels(route="POST rollback").inc()
        raise web.HTTPPreconditionFailed(reason="Version conflict")
    except ConfigValidationError as exc:
        raise web.HTTPUnprocessableEntity(
            text=json.dumps({"error": "Validation failed", "details": exc.errors}),
            content_type="application/json",
        )

    audit_svc.log(
        action="rollback",
        actor=user.user_id,
        service_name=config.service_name,
        config_type=config.config_type.value,
        config_id=str(config.id),
        key=config.key,
        change_reason=dto.change_reason,
        is_critical=config.is_critical,
        is_sensitive=config.is_sensitive,
        correlation_id=_correlation_id(request),
        source_ip=_source_ip(request),
        user_agent=request.headers.get("User-Agent"),
        value=config.value,
    )

    return web.json_response(
        _config_to_response(config, False),
        headers={"ETag": f'"{config.version}"'},
    )


@routes.get("/api/v1/config/{config_id}/history")
async def get_history(request: web.Request) -> web.Response:
    user = require_current_user(request)
    ensure_permission(user, "configs.view")

    config_id = UUID(request.match_info["config_id"])
    limit = min(int(request.rel_url.query.get("limit", "50")), 500)
    offset = int(request.rel_url.query.get("offset", "0"))

    svc = await get_config_service(request)
    try:
        history = await svc.get_history(config_id, limit, offset)
    except ConfigNotFoundError:
        raise web.HTTPNotFound()

    items = []
    for h in history:
        items.append({
            "id": str(h.id),
            "config_id": str(h.config_id),
            "version": h.version,
            "service_name": h.service_name,
            "key": h.key,
            "config_type": h.config_type.value,
            "value": h.value,
            "metadata": h.metadata,
            "is_active": h.is_active,
            "changed_by": h.changed_by,
            "change_reason": h.change_reason,
            "correlation_id": h.correlation_id,
            "changed_at": h.changed_at.isoformat(),
        })

    return web.json_response({"items": items})
