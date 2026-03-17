"""Dependency providers for aiohttp request handlers."""
from __future__ import annotations

from dataclasses import dataclass, field
from uuid import UUID

from aiohttp import web

from backend_common.db.pool import get_pool_service as get_pool
from script_service.repositories.executions import ExecutionRepository
from script_service.repositories.scripts import ScriptRepository
from script_service.services.execution_dispatcher import ExecutionDispatcher
from script_service.services.script_manager import ScriptManager
from script_service.settings import settings

_USER_ID_HEADER = "X-User-Id"
_PERMISSIONS_HEADER = "X-User-Permissions"
_SYSTEM_PERMISSIONS_HEADER = "X-User-System-Permissions"
_IS_SUPERADMIN_HEADER = "X-User-Is-Superadmin"

_SCRIPT_MANAGER_KEY = "script_manager"
_DISPATCHER_KEY = "execution_dispatcher"


@dataclass
class UserContext:
    user_id: UUID
    permissions: frozenset[str] = field(default_factory=frozenset)
    system_permissions: frozenset[str] = field(default_factory=frozenset)
    is_superadmin: bool = False


def _parse_permissions(value: str | None) -> frozenset[str]:
    if not value:
        return frozenset()
    return frozenset(p.strip() for p in value.split(",") if p.strip())


def extract_user(request: web.Request) -> UserContext:
    """Read RBAC headers set by auth-proxy and build UserContext."""
    user_header = request.headers.get(_USER_ID_HEADER)
    if user_header is None:
        raise web.HTTPUnauthorized(reason=f"Header {_USER_ID_HEADER} is required")
    try:
        user_id = UUID(user_header)
    except ValueError as exc:
        raise web.HTTPBadRequest(text=f"Invalid {_USER_ID_HEADER}") from exc

    is_superadmin_raw = request.headers.get(_IS_SUPERADMIN_HEADER, "false")
    is_superadmin = is_superadmin_raw.strip().lower() == "true"

    permissions = _parse_permissions(request.headers.get(_PERMISSIONS_HEADER))
    system_permissions = _parse_permissions(request.headers.get(_SYSTEM_PERMISSIONS_HEADER))

    return UserContext(
        user_id=user_id,
        permissions=permissions,
        system_permissions=system_permissions,
        is_superadmin=is_superadmin,
    )


def ensure_permission(user: UserContext, permission: str) -> None:
    """Raise HTTPForbidden if user lacks the given permission.

    Superadmin bypasses all checks.
    """
    if user.is_superadmin:
        return
    if permission in user.permissions or permission in user.system_permissions:
        return
    raise web.HTTPForbidden(reason=f"Missing permission: {permission}")


async def get_script_manager(request: web.Request) -> ScriptManager:
    manager = request.get(_SCRIPT_MANAGER_KEY)
    if manager is None:
        pool = await get_pool()
        manager = ScriptManager(ScriptRepository(pool))
        request[_SCRIPT_MANAGER_KEY] = manager
    return manager


async def get_execution_dispatcher(request: web.Request) -> ExecutionDispatcher:
    dispatcher = request.get(_DISPATCHER_KEY)
    if dispatcher is None:
        pool = await get_pool()
        dispatcher = ExecutionDispatcher(
            execution_repo=ExecutionRepository(pool),
            script_repo=ScriptRepository(pool),
            rabbitmq_url=settings.rabbitmq_url,
        )
        request[_DISPATCHER_KEY] = dispatcher
    return dispatcher
