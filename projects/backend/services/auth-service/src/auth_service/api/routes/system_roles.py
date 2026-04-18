"""System roles API routes."""
from __future__ import annotations

from uuid import UUID

import structlog
from aiohttp import web

from auth_service.api.utils import get_requester_id
from auth_service.core.exceptions import InvalidCredentialsError
from auth_service.domain.dto import (
    CreateRoleRequest,
    RoleResponse,
    UpdateRoleRequest,
)
from auth_service.domain.models import ScopeType
from auth_service.repositories.roles import RoleRepository
from auth_service.repositories.user_roles import UserRoleRepository
from auth_service.services.dependencies import get_permission_service
from backend_common.aiohttp_app import read_json
from backend_common.db.pool import get_pool_service as get_pool

logger = structlog.get_logger(__name__)


# =============================================================================
# System roles CRUD
# =============================================================================

async def list_system_roles(request: web.Request) -> web.Response:
    """List all system roles.

    Requires authentication. Returns both built-in and custom system roles.
    """
    try:
        perm_svc = await get_permission_service(request)
        await get_requester_id(request, perm_svc)  # authentication check only

        pool = await get_pool()
        role_repo = RoleRepository(pool)

        roles = await role_repo.list_system()

        # Fetch permissions for each role
        result = []
        for role in roles:
            perms = await role_repo.get_permissions(role.id)
            role_resp = RoleResponse.from_model(role, permissions=perms)
            result.append(role_resp.model_dump())

        return web.json_response(result)
    except InvalidCredentialsError as e:
        return web.json_response({"error": str(e)}, status=401)
    except Exception as e:
        logger.error("Failed to list system roles", exc_info=e)
        return web.json_response({"error": str(e)}, status=500)


async def get_system_role(request: web.Request) -> web.Response:
    """Get a specific system role by ID."""
    try:
        pool = await get_pool()
        role_repo = RoleRepository(pool)
        
        role_id = UUID(request.match_info["role_id"])
        role = await role_repo.get_by_id_or_raise(role_id)
        
        if role.scope_type != ScopeType.SYSTEM:
            return web.json_response({"error": "Not a system role"}, status=404)
        
        perms = await role_repo.get_permissions(role.id)
        role_resp = RoleResponse.from_model(role, permissions=perms)
        return web.json_response(role_resp.model_dump())
    except Exception as e:
        logger.error("Failed to get system role", exc_info=e)
        if hasattr(e, "status_code"):
            return web.json_response({"error": str(e)}, status=getattr(e, "status_code", 500))
        return web.json_response({"error": str(e)}, status=500)


async def create_system_role(request: web.Request) -> web.Response:
    """Create a custom system role.
    
    Requires 'roles.manage' permission.
    """
    try:
        perm_svc = await get_permission_service(request)
        requester_id = await get_requester_id(request, perm_svc)
        
        data = await read_json(request)
        req = CreateRoleRequest(**data)
        
        role = await perm_svc.create_custom_role(
            creator_id=requester_id,
            name=req.name,
            scope_type=ScopeType.SYSTEM,
            permissions=req.permissions,
            description=req.description,
        )
        
        # Fetch permissions for response
        perm_ids = await perm_svc.get_role_permissions(role.id)
        role_resp = RoleResponse.from_model(role, permissions=perm_ids)
        return web.json_response(role_resp.model_dump(), status=201)
    except InvalidCredentialsError as e:
        return web.json_response({"error": str(e)}, status=401)
    except Exception as e:
        logger.error("Failed to create system role", exc_info=e)
        if hasattr(e, "status_code"):
            return web.json_response({"error": str(e)}, status=getattr(e, "status_code", 500))
        return web.json_response({"error": str(e)}, status=500)


async def update_system_role(request: web.Request) -> web.Response:
    """Update a custom system role.
    
    Requires 'roles.manage' permission. Cannot update built-in roles.
    """
    try:
        perm_svc = await get_permission_service(request)
        requester_id = await get_requester_id(request, perm_svc)
        
        role_id = UUID(request.match_info["role_id"])
        data = await read_json(request)
        req = UpdateRoleRequest(**data)
        
        role = await perm_svc.update_custom_role(
            updater_id=requester_id,
            role_id=role_id,
            name=req.name,
            description=req.description,
            permissions=req.permissions,
        )
        
        # Fetch permissions for response
        perm_ids = await perm_svc.get_role_permissions(role.id)
        role_resp = RoleResponse.from_model(role, permissions=perm_ids)
        return web.json_response(role_resp.model_dump())
    except InvalidCredentialsError as e:
        return web.json_response({"error": str(e)}, status=401)
    except Exception as e:
        logger.error("Failed to update system role", exc_info=e)
        if hasattr(e, "status_code"):
            return web.json_response({"error": str(e)}, status=getattr(e, "status_code", 500))
        return web.json_response({"error": str(e)}, status=500)


async def delete_system_role(request: web.Request) -> web.Response:
    """Delete a custom system role.
    
    Requires 'roles.manage' permission. Cannot delete built-in roles.
    """
    try:
        perm_svc = await get_permission_service(request)
        requester_id = await get_requester_id(request, perm_svc)
        
        role_id = UUID(request.match_info["role_id"])
        await perm_svc.delete_custom_role(requester_id, role_id)
        return web.Response(status=204)
    except InvalidCredentialsError as e:
        return web.json_response({"error": str(e)}, status=401)
    except Exception as e:
        logger.error("Failed to delete system role", exc_info=e)
        if hasattr(e, "status_code"):
            return web.json_response({"error": str(e)}, status=getattr(e, "status_code", 500))
        return web.json_response({"error": str(e)}, status=500)


# =============================================================================
# User system role assignments
# =============================================================================

async def grant_system_role_to_user(request: web.Request) -> web.Response:
    """Grant a system role to a user.
    
    Requires 'roles.assign' permission.
    
    Body:
        role_id (UUID): The role to grant
        expires_at (datetime, optional): Expiration time
    """
    try:
        perm_svc = await get_permission_service(request)
        requester_id = await get_requester_id(request, perm_svc)
        
        target_user_id = UUID(request.match_info["user_id"])
        data = await read_json(request)
        role_id = UUID(data["role_id"])
        expires_at = data.get("expires_at")
        if expires_at:
            from datetime import datetime
            expires_at = datetime.fromisoformat(expires_at.replace('Z', '+00:00'))
        
        role_assignment = await perm_svc.grant_system_role(
            grantor_id=requester_id,
            target_user_id=target_user_id,
            role_id=role_id,
            expires_at=expires_at if expires_at else None,
        )
        
        return web.json_response({
            "user_id": str(role_assignment.user_id),
            "role_id": str(role_assignment.role_id),
            "granted_by": str(role_assignment.granted_by),
            "granted_at": role_assignment.granted_at.isoformat(),
            "expires_at": role_assignment.expires_at.isoformat() if role_assignment.expires_at else None,
        }, status=201)
    except InvalidCredentialsError as e:
        return web.json_response({"error": str(e)}, status=401)
    except Exception as e:
        logger.error("Failed to grant system role", exc_info=e)
        if hasattr(e, "status_code"):
            return web.json_response({"error": str(e)}, status=getattr(e, "status_code", 500))
        return web.json_response({"error": str(e)}, status=500)


async def revoke_system_role_from_user(request: web.Request) -> web.Response:
    """Revoke a system role from a user.
    
    Requires 'roles.assign' permission. Protects the last superadmin.
    """
    try:
        perm_svc = await get_permission_service(request)
        requester_id = await get_requester_id(request, perm_svc)
        
        target_user_id = UUID(request.match_info["user_id"])
        role_id = UUID(request.match_info["role_id"])
        
        success = await perm_svc.revoke_system_role(
            grantor_id=requester_id,
            target_user_id=target_user_id,
            role_id=role_id,
        )
        
        if success:
            return web.json_response({"message": "Role revoked"}, status=200)
        else:
            return web.json_response({"error": "Role assignment not found"}, status=404)
    except InvalidCredentialsError as e:
        return web.json_response({"error": str(e)}, status=401)
    except Exception as e:
        logger.error("Failed to revoke system role", exc_info=e)
        if hasattr(e, "status_code"):
            return web.json_response({"error": str(e)}, status=getattr(e, "status_code", 500))
        return web.json_response({"error": str(e)}, status=500)


async def list_user_system_roles(request: web.Request) -> web.Response:
    """List system roles assigned to a user."""
    try:
        perm_svc = await get_permission_service(request)
        requester_id = await get_requester_id(request, perm_svc)
        
        target_user_id = UUID(request.match_info["user_id"])
        
        # Check permissions
        if requester_id != target_user_id:
            await perm_svc.ensure_permission(requester_id, "users.list")
        
        pool = await get_pool()
        role_repo = RoleRepository(pool)
        user_role_repo = UserRoleRepository(pool)
        
        assignments = await user_role_repo.list_system_roles(target_user_id)
        
        result = []
        for assignment in assignments:
            role = await role_repo.get_by_id(assignment.role_id)
            if role is None:
                continue  # skip orphaned assignments (role was deleted)
            result.append({
                "role_id": str(role.id),
                "role_name": role.name,
                "granted_by": str(assignment.granted_by),
                "granted_at": assignment.granted_at.isoformat(),
                "expires_at": assignment.expires_at.isoformat() if assignment.expires_at else None,
            })

        return web.json_response(result)
    except InvalidCredentialsError as e:
        return web.json_response({"error": str(e)}, status=401)
    except Exception as e:
        logger.error("Failed to list user system roles", exc_info=e)
        if hasattr(e, "status_code"):
            return web.json_response({"error": str(e)}, status=getattr(e, "status_code", 500))
        return web.json_response({"error": str(e)}, status=500)


def setup_routes(app: web.Application) -> None:
    """Setup system roles routes."""
    # System roles CRUD
    app.router.add_get("/api/v1/system-roles", list_system_roles, name="list_system_roles")
    app.router.add_get("/api/v1/system-roles/{role_id}", get_system_role, name="get_system_role")
    app.router.add_post("/api/v1/system-roles", create_system_role, name="create_system_role")
    app.router.add_patch("/api/v1/system-roles/{role_id}", update_system_role, name="update_system_role")
    app.router.add_delete("/api/v1/system-roles/{role_id}", delete_system_role, name="delete_system_role")
    
    # User role assignments
    app.router.add_get(
        "/api/v1/users/{user_id}/system-roles",
        list_user_system_roles,
        name="list_user_system_roles",
    )
    app.router.add_post(
        "/api/v1/users/{user_id}/system-roles",
        grant_system_role_to_user,
        name="grant_system_role_to_user",
    )
    app.router.add_delete(
        "/api/v1/users/{user_id}/system-roles/{role_id}",
        revoke_system_role_from_user,
        name="revoke_system_role_from_user",
    )
