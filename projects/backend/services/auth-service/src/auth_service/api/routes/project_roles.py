"""Project roles API routes."""
from __future__ import annotations

from uuid import UUID

import structlog
from aiohttp import web

from auth_service.core.exceptions import InvalidCredentialsError
from auth_service.domain.dto import CreateRoleRequest, RoleResponse, UpdateRoleRequest
from auth_service.domain.models import ScopeType
from auth_service.repositories.permissions import PermissionRepository
from auth_service.repositories.roles import RoleRepository
from auth_service.repositories.user_roles import UserRoleRepository
from auth_service.services.permission import PermissionService
from backend_common.db.pool import get_pool_service as get_pool

logger = structlog.get_logger(__name__)


async def _get_permission_service(request: web.Request) -> PermissionService:
    """Get permission service from request."""
    pool = await get_pool()
    return PermissionService(
        PermissionRepository(pool),
        RoleRepository(pool),
        UserRoleRepository(pool),
    )


def _extract_bearer_token(request: web.Request) -> str | None:
    """Extract Bearer token from Authorization header."""
    auth_header = request.headers.get("Authorization")
    if not auth_header or not auth_header.startswith("Bearer "):
        return None
    return auth_header[7:].strip() or None


async def _get_requester_id(request: web.Request, perm_svc: PermissionService) -> UUID:
    """Extract and validate requester user ID from Bearer token."""
    token = _extract_bearer_token(request)
    if not token:
        raise InvalidCredentialsError("Unauthorized")
    
    from auth_service.services.jwt import get_user_id_from_token
    user_id = UUID(get_user_id_from_token(token))
    
    # Verify user exists and is active
    from auth_service.repositories.users import UserRepository
    user_repo = UserRepository(pool=await get_pool())
    user = await user_repo.get_by_id(user_id)
    if not user or not user.is_active:
        raise InvalidCredentialsError("User not found or inactive")
    
    return user_id


# =============================================================================
# Project roles CRUD (custom roles within a project)
# =============================================================================

async def list_project_roles(request: web.Request) -> web.Response:
    """List all project roles for a specific project.
    
    Requires 'project.members.view' permission in the project.
    Returns both built-in and custom project roles.
    """
    try:
        perm_svc = await _get_permission_service(request)
        requester_id = await _get_requester_id(request, perm_svc)
        
        project_id = UUID(request.match_info["project_id"])
        
        # Check permission
        await perm_svc.ensure_permission(requester_id, "project.members.view", project_id)
        
        pool = await get_pool()
        role_repo = RoleRepository(pool)
        
        roles = await role_repo.list_by_project(project_id)
        
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
        logger.error("Failed to list project roles", exc_info=e)
        if hasattr(e, "status_code"):
            return web.json_response({"error": str(e)}, status=getattr(e, "status_code", 500))
        return web.json_response({"error": str(e)}, status=500)


async def get_project_role(request: web.Request) -> web.Response:
    """Get a specific project role by ID."""
    try:
        perm_svc = await _get_permission_service(request)
        requester_id = await _get_requester_id(request, perm_svc)
        
        project_id = UUID(request.match_info["project_id"])
        role_id = UUID(request.match_info["role_id"])
        
        # Check permission
        await perm_svc.ensure_permission(requester_id, "project.members.view", project_id)
        
        pool = await get_pool()
        role_repo = RoleRepository(pool)
        
        role = await role_repo.get_by_id_or_raise(role_id)
        
        if role.scope_type != ScopeType.PROJECT or role.project_id != project_id:
            return web.json_response({"error": "Role not found in this project"}, status=404)
        
        perms = await role_repo.get_permissions(role.id)
        role_resp = RoleResponse.from_model(role, permissions=perms)
        return web.json_response(role_resp.model_dump())
    except InvalidCredentialsError as e:
        return web.json_response({"error": str(e)}, status=401)
    except Exception as e:
        logger.error("Failed to get project role", exc_info=e)
        if hasattr(e, "status_code"):
            return web.json_response({"error": str(e)}, status=getattr(e, "status_code", 500))
        return web.json_response({"error": str(e)}, status=500)


async def create_project_role(request: web.Request) -> web.Response:
    """Create a custom project role.
    
    Requires 'project.roles.manage' permission in the project.
    """
    try:
        perm_svc = await _get_permission_service(request)
        requester_id = await _get_requester_id(request, perm_svc)
        
        project_id = UUID(request.match_info["project_id"])
        data = await request.json()
        req = CreateRoleRequest(**data)
        
        role = await perm_svc.create_custom_role(
            creator_id=requester_id,
            name=req.name,
            scope_type=ScopeType.PROJECT,
            permissions=req.permissions,
            project_id=project_id,
            description=req.description,
        )
        
        # Fetch permissions for response
        perm_ids = await perm_svc.get_role_permissions(role.id)
        role_resp = RoleResponse.from_model(role, permissions=perm_ids)
        return web.json_response(role_resp.model_dump(), status=201)
    except InvalidCredentialsError as e:
        return web.json_response({"error": str(e)}, status=401)
    except Exception as e:
        logger.error("Failed to create project role", exc_info=e)
        if hasattr(e, "status_code"):
            return web.json_response({"error": str(e)}, status=getattr(e, "status_code", 500))
        return web.json_response({"error": str(e)}, status=500)


async def update_project_role(request: web.Request) -> web.Response:
    """Update a custom project role.
    
    Requires 'project.roles.manage' permission. Cannot update built-in roles.
    """
    try:
        perm_svc = await _get_permission_service(request)
        requester_id = await _get_requester_id(request, perm_svc)
        
        project_id = UUID(request.match_info["project_id"])
        role_id = UUID(request.match_info["role_id"])
        data = await request.json()
        req = UpdateRoleRequest(**data)

        # Verify role belongs to this project BEFORE making changes
        existing_role = await perm_svc.get_role_by_id_or_raise(role_id)
        if existing_role.project_id != project_id:
            return web.json_response({"error": "Role not found in this project"}, status=404)

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
        logger.error("Failed to update project role", exc_info=e)
        if hasattr(e, "status_code"):
            return web.json_response({"error": str(e)}, status=getattr(e, "status_code", 500))
        return web.json_response({"error": str(e)}, status=500)


async def delete_project_role(request: web.Request) -> web.Response:
    """Delete a custom project role.
    
    Requires 'project.roles.manage' permission. Cannot delete built-in roles.
    """
    try:
        perm_svc = await _get_permission_service(request)
        requester_id = await _get_requester_id(request, perm_svc)
        
        project_id = UUID(request.match_info["project_id"])
        role_id = UUID(request.match_info["role_id"])
        
        role = await perm_svc.get_role_by_id_or_raise(role_id)
        if role.project_id != project_id:
            return web.json_response({"error": "Role not found in this project"}, status=404)
        
        await perm_svc.delete_custom_role(requester_id, role_id)
        
        return web.json_response({"message": "Role deleted"}, status=200)
    except InvalidCredentialsError as e:
        return web.json_response({"error": str(e)}, status=401)
    except Exception as e:
        logger.error("Failed to delete project role", exc_info=e)
        if hasattr(e, "status_code"):
            return web.json_response({"error": str(e)}, status=getattr(e, "status_code", 500))
        return web.json_response({"error": str(e)}, status=500)


# =============================================================================
# Project member role assignments
# =============================================================================

async def list_member_roles(request: web.Request) -> web.Response:
    """List all roles assigned to a specific user in a project.
    
    Requires 'project.members.view' permission.
    """
    try:
        perm_svc = await _get_permission_service(request)
        requester_id = await _get_requester_id(request, perm_svc)
        
        project_id = UUID(request.match_info["project_id"])
        user_id = UUID(request.match_info["user_id"])
        
        # Check permission
        await perm_svc.ensure_permission(requester_id, "project.members.view", project_id)
        
        pool = await get_pool()
        role_repo = RoleRepository(pool)
        user_role_repo = UserRoleRepository(pool)
        
        assignments = await user_role_repo.list_project_roles(user_id, project_id)
        
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
        logger.error("Failed to list member roles", exc_info=e)
        if hasattr(e, "status_code"):
            return web.json_response({"error": str(e)}, status=getattr(e, "status_code", 500))
        return web.json_response({"error": str(e)}, status=500)


async def grant_role_to_member(request: web.Request) -> web.Response:
    """Grant a project role to a user (add member or add additional role).
    
    Requires 'project.members.change_role' permission.
    
    Body:
        role_id (UUID): The role to grant
        expires_at (datetime, optional): Expiration time
    """
    try:
        perm_svc = await _get_permission_service(request)
        requester_id = await _get_requester_id(request, perm_svc)
        
        project_id = UUID(request.match_info["project_id"])
        user_id = UUID(request.match_info["user_id"])
        
        data = await request.json()
        role_id = UUID(data["role_id"])
        expires_at = data.get("expires_at")
        if expires_at:
            from datetime import datetime, timezone
            expires_at = datetime.fromisoformat(expires_at.replace('Z', '+00:00'))
        
        role_assignment = await perm_svc.grant_project_role(
            grantor_id=requester_id,
            project_id=project_id,
            target_user_id=user_id,
            role_id=role_id,
        )
        
        return web.json_response({
            "user_id": str(role_assignment.user_id),
            "project_id": str(role_assignment.project_id),
            "role_id": str(role_assignment.role_id),
            "granted_by": str(role_assignment.granted_by),
            "granted_at": role_assignment.granted_at.isoformat(),
            "expires_at": role_assignment.expires_at.isoformat() if role_assignment.expires_at else None,
        }, status=201)
    except InvalidCredentialsError as e:
        return web.json_response({"error": str(e)}, status=401)
    except Exception as e:
        logger.error("Failed to grant project role", exc_info=e)
        if hasattr(e, "status_code"):
            return web.json_response({"error": str(e)}, status=getattr(e, "status_code", 500))
        return web.json_response({"error": str(e)}, status=500)


async def revoke_role_from_member(request: web.Request) -> web.Response:
    """Revoke a project role from a user.
    
    Requires 'project.members.change_role' permission. Protects the last owner.
    """
    try:
        perm_svc = await _get_permission_service(request)
        requester_id = await _get_requester_id(request, perm_svc)
        
        project_id = UUID(request.match_info["project_id"])
        user_id = UUID(request.match_info["user_id"])
        role_id = UUID(request.match_info["role_id"])
        
        success = await perm_svc.revoke_project_role(
            grantor_id=requester_id,
            project_id=project_id,
            target_user_id=user_id,
            role_id=role_id,
        )
        
        if success:
            return web.json_response({"message": "Role revoked"}, status=200)
        else:
            return web.json_response({"error": "Role assignment not found"}, status=404)
    except InvalidCredentialsError as e:
        return web.json_response({"error": str(e)}, status=401)
    except Exception as e:
        logger.error("Failed to revoke project role", exc_info=e)
        if hasattr(e, "status_code"):
            return web.json_response({"error": str(e)}, status=getattr(e, "status_code", 500))
        return web.json_response({"error": str(e)}, status=500)


def setup_routes(app: web.Application) -> None:
    """Setup project roles routes."""
    # Project roles CRUD
    app.router.add_get(
        "/api/v1/projects/{project_id}/roles",
        list_project_roles,
        name="list_project_roles",
    )
    app.router.add_get(
        "/api/v1/projects/{project_id}/roles/{role_id}",
        get_project_role,
        name="get_project_role",
    )
    app.router.add_post(
        "/api/v1/projects/{project_id}/roles",
        create_project_role,
        name="create_project_role",
    )
    app.router.add_patch(
        "/api/v1/projects/{project_id}/roles/{role_id}",
        update_project_role,
        name="update_project_role",
    )
    app.router.add_delete(
        "/api/v1/projects/{project_id}/roles/{role_id}",
        delete_project_role,
        name="delete_project_role",
    )
    
    # Member role assignments
    app.router.add_get(
        "/api/v1/projects/{project_id}/members/{user_id}/roles",
        list_member_roles,
        name="list_member_roles",
    )
    app.router.add_post(
        "/api/v1/projects/{project_id}/members/{user_id}/roles",
        grant_role_to_member,
        name="grant_role_to_member",
    )
    app.router.add_delete(
        "/api/v1/projects/{project_id}/members/{user_id}/roles/{role_id}",
        revoke_role_from_member,
        name="revoke_role_from_member",
    )
