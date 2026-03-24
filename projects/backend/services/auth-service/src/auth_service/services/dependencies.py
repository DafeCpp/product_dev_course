"""Centralized dependency providers for auth-service aiohttp handlers.

Mirrors the pattern from experiment-service: each service is created once per
request and cached in ``request[_KEY]`` so that multiple route helpers that
need the same service within a single request share the instance.
"""
from __future__ import annotations

from typing import Awaitable, Callable, TypeVar

from aiohttp import web

from backend_common.db.pool import get_pool_service as get_pool

from auth_service.repositories.audit import AuditRepository
from auth_service.repositories.invites import InviteRepository
from auth_service.repositories.password_reset import PasswordResetRepository
from auth_service.repositories.permissions import PermissionRepository
from auth_service.repositories.revoked_tokens import RevokedTokenRepository
from auth_service.repositories.roles import RoleRepository
from auth_service.repositories.token_families import TokenFamilyRepository
from auth_service.repositories.user_roles import UserRoleRepository
from auth_service.repositories.users import UserRepository
from auth_service.repositories.projects import ProjectRepository
from auth_service.services.auth import AuthService
from auth_service.services.permission import PermissionService
from auth_service.services.projects import ProjectService
from auth_service.settings import settings

TService = TypeVar("TService")

_AUTH_SERVICE_KEY = "_dep_auth_service"
_PROJECT_SERVICE_KEY = "_dep_project_service"
_PERMISSION_SERVICE_KEY = "_dep_permission_service"


async def _get_or_create(
    request: web.Request,
    cache_key: str,
    builder: Callable[[web.Request], Awaitable[TService]],
) -> TService:
    service = request.get(cache_key)
    if service is None:
        service = await builder(request)
        request[cache_key] = service
    return service


async def get_auth_service(request: web.Request) -> AuthService:
    async def builder(_: web.Request) -> AuthService:
        pool = await get_pool()
        user_repo = UserRepository(pool)
        revoked_repo = RevokedTokenRepository(pool)
        reset_repo = PasswordResetRepository(pool)
        invite_repo = InviteRepository(pool)
        audit_repo = AuditRepository(pool)
        perm_service = PermissionService(
            PermissionRepository(pool),
            RoleRepository(pool),
            UserRoleRepository(pool),
            audit_repo=audit_repo,
        )
        family_repo = TokenFamilyRepository(pool)
        return AuthService(
            user_repo,
            revoked_repo,
            reset_repo,
            perm_service,
            invite_repo=invite_repo,
            registration_mode=settings.registration_mode,
            audit_repo=audit_repo,
            family_repo=family_repo,
        )

    return await _get_or_create(request, _AUTH_SERVICE_KEY, builder)


async def get_project_service(request: web.Request) -> ProjectService:
    async def builder(_: web.Request) -> ProjectService:
        pool = await get_pool()
        project_repo = ProjectRepository(pool)
        user_repo = UserRepository(pool)
        user_role_repo = UserRoleRepository(pool)
        audit_repo = AuditRepository(pool)
        perm_svc = PermissionService(
            PermissionRepository(pool),
            RoleRepository(pool),
            user_role_repo,
            audit_repo=audit_repo,
        )
        return ProjectService(
            project_repo, user_repo, user_role_repo, perm_svc, audit_repo=audit_repo,
        )

    return await _get_or_create(request, _PROJECT_SERVICE_KEY, builder)


async def get_permission_service(request: web.Request) -> PermissionService:
    async def builder(_: web.Request) -> PermissionService:
        pool = await get_pool()
        return PermissionService(
            PermissionRepository(pool),
            RoleRepository(pool),
            UserRoleRepository(pool),
            audit_repo=AuditRepository(pool),
        )

    return await _get_or_create(request, _PERMISSION_SERVICE_KEY, builder)
