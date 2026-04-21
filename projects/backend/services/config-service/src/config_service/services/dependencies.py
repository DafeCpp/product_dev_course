"""Dependency injection and RBAC helpers."""
from __future__ import annotations

from dataclasses import dataclass
from typing import Awaitable, Callable, TypeVar

from aiohttp import web

from backend_common.db.pool import get_pool
from config_service.repositories.config_repo import ConfigRepository
from config_service.repositories.history_repo import HistoryRepository
from config_service.repositories.idempotency_repo import IdempotencyRepository
from config_service.repositories.schema_repo import SchemaRepository
from config_service.services.audit_service import AuditService
from config_service.services.bulk_service import BulkService
from config_service.services.config_service import ConfigService
from config_service.services.idempotency import IdempotencyService
from config_service.services.schema_service import SchemaService
from config_service.services.validation_service import ValidationService

TService = TypeVar("TService")

_CONFIG_SVC_KEY = "config_service"
_SCHEMA_SVC_KEY = "schema_service"
_VALIDATION_SVC_KEY = "validation_service"
_BULK_SVC_KEY = "bulk_service"
_IDEMPOTENCY_SVC_KEY = "idempotency_service"
_AUDIT_SVC_KEY = "audit_service"


@dataclass(frozen=True)
class UserContext:
    user_id: str
    is_superadmin: bool
    system_permissions: frozenset[str]
    project_permissions: dict[str, list[str]]


def require_current_user(request: web.Request) -> UserContext:
    user_id = request.headers.get("X-User-Id")
    if not user_id:
        raise web.HTTPUnauthorized(reason="Missing X-User-Id header")
    is_superadmin = request.headers.get("X-User-Is-Superadmin", "false").lower() == "true"
    raw_sys = request.headers.get("X-User-System-Permissions", "")
    system_permissions = frozenset(p.strip() for p in raw_sys.split(",") if p.strip())
    raw_proj = request.headers.get("X-User-Permissions", "")
    project_permissions: dict[str, list[str]] = {}
    for entry in raw_proj.split(";"):
        entry = entry.strip()
        if ":" in entry:
            proj, perms = entry.split(":", 1)
            project_permissions[proj.strip()] = [p.strip() for p in perms.split(",") if p.strip()]
    return UserContext(
        user_id=user_id,
        is_superadmin=is_superadmin,
        system_permissions=system_permissions,
        project_permissions=project_permissions,
    )


def ensure_permission(user: UserContext, permission: str) -> None:
    if user.is_superadmin:
        return
    if permission in user.system_permissions:
        return
    raise web.HTTPForbidden(reason=f"Missing permission: {permission}")


async def _get_or_create_service(
    request: web.Request,
    cache_key: str,
    builder: Callable[[web.Request], Awaitable[TService]],
) -> TService:
    service = request.get(cache_key)
    if service is None:
        service = await builder(request)
        request[cache_key] = service
    return service


async def get_validation_service(request: web.Request) -> ValidationService:
    async def builder(_: web.Request) -> ValidationService:
        pool = await get_pool()
        return ValidationService(SchemaRepository(pool))

    return await _get_or_create_service(request, _VALIDATION_SVC_KEY, builder)


async def get_schema_service(request: web.Request) -> SchemaService:
    async def builder(_: web.Request) -> SchemaService:
        pool = await get_pool()
        return SchemaService(SchemaRepository(pool), ConfigRepository(pool))

    return await _get_or_create_service(request, _SCHEMA_SVC_KEY, builder)


async def get_config_service(request: web.Request) -> ConfigService:
    async def builder(req: web.Request) -> ConfigService:
        pool = await get_pool()
        return ConfigService(
            ConfigRepository(pool),
            HistoryRepository(pool),
            await get_validation_service(req),
        )

    return await _get_or_create_service(request, _CONFIG_SVC_KEY, builder)


async def get_bulk_service(request: web.Request) -> BulkService:
    async def builder(req: web.Request) -> BulkService:
        pool = await get_pool()
        return BulkService(ConfigRepository(pool), await get_validation_service(req))

    return await _get_or_create_service(request, _BULK_SVC_KEY, builder)


async def get_idempotency_service(request: web.Request) -> IdempotencyService:
    async def builder(_: web.Request) -> IdempotencyService:
        pool = await get_pool()
        return IdempotencyService(IdempotencyRepository(pool))

    return await _get_or_create_service(request, _IDEMPOTENCY_SVC_KEY, builder)


async def get_audit_service(request: web.Request) -> AuditService:
    async def builder(_: web.Request) -> AuditService:
        return AuditService()

    return await _get_or_create_service(request, _AUDIT_SVC_KEY, builder)
