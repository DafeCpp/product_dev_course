"""Permissions API routes."""
from __future__ import annotations

from uuid import UUID

import structlog
from aiohttp import web

from auth_service.api.utils import get_requester_id
from auth_service.core.exceptions import InvalidCredentialsError
from auth_service.domain.dto import PermissionResponse
from auth_service.repositories.permissions import PermissionRepository
from auth_service.repositories.roles import RoleRepository
from auth_service.repositories.user_roles import UserRoleRepository
from auth_service.services.permission import PermissionService
from backend_common.db.pool import get_pool_service as get_pool

logger = structlog.get_logger(__name__)


async def _get_permission_service(request: web.Request) -> PermissionService:
    pool = await get_pool()
    return PermissionService(
        PermissionRepository(pool),
        RoleRepository(pool),
        UserRoleRepository(pool),
    )


async def list_permissions(request: web.Request) -> web.Response:
    """List all permissions (catalog).

    Requires authentication. Returns the full permissions catalog.
    """
    try:
        perm_svc = await _get_permission_service(request)
        await get_requester_id(request, perm_svc)  # authentication check only

        pool = await get_pool()
        perm_repo = PermissionRepository(pool)
        permissions = await perm_repo.list_all()

        response = [PermissionResponse.from_model(p).model_dump() for p in permissions]
        return web.json_response(response)
    except InvalidCredentialsError as e:
        return web.json_response({"error": str(e)}, status=401)
    except Exception as e:
        logger.error("Failed to list permissions", exc_info=e)
        return web.json_response({"error": str(e)}, status=500)


async def get_effective_permissions(request: web.Request) -> web.Response:
    """Get effective permissions for a user.
    
    Requires authentication. Users can view their own permissions,
    or users with 'users.list' permission can view others' permissions.
    
    Query params:
        project_id (optional): Filter to project-specific permissions
    """
    try:
        perm_svc = await _get_permission_service(request)
        requester_id = await get_requester_id(request, perm_svc)
        
        # Get target user ID from path
        target_user_id = UUID(request.match_info["user_id"])
        
        # Check if requester can view target's permissions
        if requester_id != target_user_id:
            await perm_svc.ensure_permission(requester_id, "users.list")
        
        # Get project_id from query params (optional)
        project_id_str = request.query.get("project_id")
        project_id = UUID(project_id_str) if project_id_str else None
        
        # If project_id is provided, check requester has access to that project
        if project_id is not None and requester_id != target_user_id:
            await perm_svc.ensure_permission(requester_id, "project.members.view", project_id)
        
        effective_perms = await perm_svc.get_effective_permissions(target_user_id, project_id)
        return web.json_response(effective_perms.model_dump())
    except InvalidCredentialsError as e:
        return web.json_response({"error": str(e)}, status=401)
    except Exception as e:
        logger.error("Failed to get effective permissions", exc_info=e)
        if hasattr(e, "status_code"):
            return web.json_response({"error": str(e)}, status=getattr(e, "status_code", 500))
        return web.json_response({"error": str(e)}, status=500)


def setup_routes(app: web.Application) -> None:
    """Setup permissions routes."""
    app.router.add_get("/api/v1/permissions", list_permissions, name="list_permissions")
    app.router.add_get(
        "/api/v1/users/{user_id}/effective-permissions",
        get_effective_permissions,
        name="get_effective_permissions",
    )
