"""Project routes."""
from __future__ import annotations

import structlog
from uuid import UUID

from aiohttp import web

from auth_service.api.utils import extract_client_ip, extract_user_agent, get_requester_id
from auth_service.core.exceptions import AuthError, ForbiddenError, NotFoundError, handle_auth_error
from auth_service.domain.dto import (
    ProjectCreateRequest,
    ProjectListResponse,
    ProjectMemberAddRequest,
    ProjectMemberResponse,
    ProjectMemberUpdateRequest,
    ProjectResponse,
    ProjectUpdateRequest,
)
from auth_service.services.dependencies import get_permission_service, get_project_service

logger = structlog.get_logger(__name__)


def _parse_project_id(request: web.Request) -> UUID:
    """Parse project_id from URL match info, raising 400 on invalid UUID."""
    try:
        return UUID(request.match_info["project_id"])
    except ValueError:
        raise web.HTTPBadRequest(text="Invalid project ID")


async def create_project(request: web.Request) -> web.Response:
    """Create a new project. Requires 'projects.create' permission."""
    try:
        perm_svc = await get_permission_service(request)
        user_id = await get_requester_id(request, perm_svc)
    except AuthError as e:
        return handle_auth_error(request, e)

    try:
        data = await request.json()
        req = ProjectCreateRequest(**data)
    except Exception as e:
        return web.json_response({"error": f"Invalid request: {e}"}, status=400)

    try:
        service = await get_project_service(request)
        project = await service.create_project(
            name=req.name,
            description=req.description,
            owner_id=user_id,
            ip_address=extract_client_ip(request),
            user_agent=extract_user_agent(request),
        )
        return web.json_response(
            ProjectResponse.from_project(project).model_dump(),
            status=201,
        )
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("Create project error")
        return web.json_response({"error": "Internal server error"}, status=500)


async def get_project(request: web.Request) -> web.Response:
    """Get project by ID. Requires 'project.members.view' permission."""
    try:
        perm_svc = await get_permission_service(request)
        user_id = await get_requester_id(request, perm_svc)
    except AuthError as e:
        return handle_auth_error(request, e)

    try:
        project_id = _parse_project_id(request)
    except web.HTTPBadRequest:
        return web.json_response({"error": "Invalid project ID"}, status=400)

    try:
        service = await get_project_service(request)
        project = await service.get_project(project_id, user_id)
        return web.json_response(
            ProjectResponse.from_project(project).model_dump(),
            status=200,
        )
    except NotFoundError as e:
        return web.json_response({"error": str(e)}, status=404)
    except ForbiddenError as e:
        return web.json_response({"error": str(e)}, status=403)
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("Get project error")
        return web.json_response({"error": "Internal server error"}, status=500)


async def list_projects(request: web.Request) -> web.Response:
    """List projects for current user with optional search and role filter.

    Query parameters:
      search  — case-insensitive substring match on project name
      role    — filter by role name (e.g. owner, editor, viewer)
      limit   — page size, 1-100, default 20
      offset  — page offset, default 0
    """
    try:
        perm_svc = await get_permission_service(request)
        user_id = await get_requester_id(request, perm_svc)
    except AuthError as e:
        return handle_auth_error(request, e)

    search: str | None = request.query.get("search") or None
    role: str | None = request.query.get("role") or None

    try:
        limit = min(max(int(request.query.get("limit", "20")), 1), 100)
        offset = max(int(request.query.get("offset", "0")), 0)
    except ValueError:
        return web.json_response({"error": "limit and offset must be integers"}, status=400)

    try:
        service = await get_project_service(request)
        projects, total = await service.list_user_projects(
            user_id, search=search, role=role, limit=limit, offset=offset,
        )
        response = ProjectListResponse(
            items=[ProjectResponse.from_project(p) for p in projects],
            total=total,
            limit=limit,
            offset=offset,
        )
        return web.json_response(response.model_dump(), status=200)
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("List projects error")
        return web.json_response({"error": "Internal server error"}, status=500)


async def update_project(request: web.Request) -> web.Response:
    """Update project. Requires 'project.settings.update' permission."""
    try:
        perm_svc = await get_permission_service(request)
        user_id = await get_requester_id(request, perm_svc)
    except AuthError as e:
        return handle_auth_error(request, e)

    try:
        project_id = _parse_project_id(request)
    except web.HTTPBadRequest:
        return web.json_response({"error": "Invalid project ID"}, status=400)

    try:
        data = await request.json()
        req = ProjectUpdateRequest(**data)
    except Exception as e:
        return web.json_response({"error": f"Invalid request: {e}"}, status=400)

    try:
        service = await get_project_service(request)
        project = await service.update_project(
            project_id=project_id,
            user_id=user_id,
            name=req.name,
            description=req.description,
        )
        return web.json_response(
            ProjectResponse.from_project(project).model_dump(),
            status=200,
        )
    except NotFoundError as e:
        return web.json_response({"error": str(e)}, status=404)
    except ForbiddenError as e:
        return web.json_response({"error": str(e)}, status=403)
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("Update project error")
        return web.json_response({"error": "Internal server error"}, status=500)


async def delete_project(request: web.Request) -> web.Response:
    """Delete project. Requires 'project.settings.delete' permission."""
    try:
        perm_svc = await get_permission_service(request)
        user_id = await get_requester_id(request, perm_svc)
    except AuthError as e:
        return handle_auth_error(request, e)

    try:
        project_id = _parse_project_id(request)
    except web.HTTPBadRequest:
        return web.json_response({"error": "Invalid project ID"}, status=400)

    try:
        service = await get_project_service(request)
        await service.delete_project(
            project_id, user_id,
            ip_address=extract_client_ip(request),
            user_agent=extract_user_agent(request),
        )
        return web.json_response({"ok": True}, status=200)
    except NotFoundError as e:
        return web.json_response({"error": str(e)}, status=404)
    except ForbiddenError as e:
        return web.json_response({"error": str(e)}, status=403)
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("Delete project error")
        return web.json_response({"error": "Internal server error"}, status=500)


async def list_members(request: web.Request) -> web.Response:
    """List project members. Requires 'project.members.view' permission."""
    try:
        perm_svc = await get_permission_service(request)
        user_id = await get_requester_id(request, perm_svc)
    except AuthError as e:
        return handle_auth_error(request, e)

    try:
        project_id = _parse_project_id(request)
    except web.HTTPBadRequest:
        return web.json_response({"error": "Invalid project ID"}, status=400)

    try:
        service = await get_project_service(request)
        members = await service.list_members(project_id, user_id)
        return web.json_response(
            {
                "members": [
                    ProjectMemberResponse(
                        project_id=str(m["project_id"]),
                        user_id=str(m["user_id"]),
                        roles=m["roles"],
                        granted_at=m["granted_at"].isoformat() if m.get("granted_at") else None,
                        username=m.get("username"),
                    ).model_dump()
                    for m in members
                ]
            },
            status=200,
        )
    except NotFoundError as e:
        return web.json_response({"error": str(e)}, status=404)
    except ForbiddenError as e:
        return web.json_response({"error": str(e)}, status=403)
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("List members error")
        return web.json_response({"error": "Internal server error"}, status=500)


async def add_member(request: web.Request) -> web.Response:
    """Add member to project. Requires 'project.members.invite' permission."""
    try:
        perm_svc = await get_permission_service(request)
        user_id = await get_requester_id(request, perm_svc)
    except AuthError as e:
        return handle_auth_error(request, e)

    try:
        project_id = _parse_project_id(request)
    except web.HTTPBadRequest:
        return web.json_response({"error": "Invalid project ID"}, status=400)

    try:
        data = await request.json()
        req = ProjectMemberAddRequest(**data)
    except Exception as e:
        return web.json_response({"error": f"Invalid request: {e}"}, status=400)

    try:
        new_user_id = UUID(req.user_id)
        role_id = UUID(req.resolved_role_id())
    except ValueError as e:
        return web.json_response({"error": str(e)}, status=400)

    try:
        service = await get_project_service(request)
        assignment = await service.add_member(
            project_id=project_id,
            requester_id=user_id,
            new_user_id=new_user_id,
            role_id=role_id,
            ip_address=extract_client_ip(request),
            user_agent=extract_user_agent(request),
        )
        return web.json_response(
            {
                "user_id": str(assignment.user_id),
                "project_id": str(assignment.project_id),
                "role_id": str(assignment.role_id),
                "granted_by": str(assignment.granted_by),
                "granted_at": assignment.granted_at.isoformat(),
                "expires_at": assignment.expires_at.isoformat() if assignment.expires_at else None,
            },
            status=201,
        )
    except NotFoundError as e:
        return web.json_response({"error": str(e)}, status=404)
    except ForbiddenError as e:
        return web.json_response({"error": str(e)}, status=403)
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("Add member error")
        return web.json_response({"error": "Internal server error"}, status=500)


async def remove_member(request: web.Request) -> web.Response:
    """Remove member from project. Requires 'project.members.remove' permission."""
    try:
        perm_svc = await get_permission_service(request)
        user_id = await get_requester_id(request, perm_svc)
    except AuthError as e:
        return handle_auth_error(request, e)

    try:
        project_id = UUID(request.match_info["project_id"])
        member_user_id = UUID(request.match_info["user_id"])
    except ValueError:
        return web.json_response({"error": "Invalid ID"}, status=400)

    # Get role_id from query param or body
    try:
        data = await request.json() if request.can_read_body else {}
        role_id_str = data.get("role_id") or request.query.get("role_id")
        if not role_id_str:
            return web.json_response({"error": "role_id required"}, status=400)
        role_id = UUID(role_id_str)
    except ValueError:
        return web.json_response({"error": "Invalid role ID"}, status=400)

    try:
        service = await get_project_service(request)
        await service.remove_member(
            project_id=project_id,
            requester_id=user_id,
            member_user_id=member_user_id,
            role_id=role_id,
            ip_address=extract_client_ip(request),
            user_agent=extract_user_agent(request),
        )
        return web.json_response({"ok": True}, status=200)
    except NotFoundError as e:
        return web.json_response({"error": str(e)}, status=404)
    except ForbiddenError as e:
        return web.json_response({"error": str(e)}, status=403)
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("Remove member error")
        return web.json_response({"error": "Internal server error"}, status=500)


async def update_member_role(request: web.Request) -> web.Response:
    """Update member role. Requires 'project.members.change_role' permission."""
    try:
        perm_svc = await get_permission_service(request)
        user_id = await get_requester_id(request, perm_svc)
    except AuthError as e:
        return handle_auth_error(request, e)

    try:
        project_id = UUID(request.match_info["project_id"])
        member_user_id = UUID(request.match_info["user_id"])
    except ValueError:
        return web.json_response({"error": "Invalid ID"}, status=400)

    try:
        data = await request.json()
        req = ProjectMemberUpdateRequest(**data)
    except Exception as e:
        return web.json_response({"error": f"Invalid request: {e}"}, status=400)

    try:
        new_role_id = UUID(req.resolved_role_id())
        # Get old role_id from query param (also supports role name)
        old_role_id_str = request.query.get("old_role_id") or request.query.get("old_role")
        if not old_role_id_str:
            return web.json_response({"error": "old_role_id query param required"}, status=400)
        from auth_service.domain.dto import PROJECT_ROLE_NAME_TO_ID as _RMAP
        old_role_id = UUID(_RMAP.get(old_role_id_str.lower(), old_role_id_str))
    except ValueError as e:
        return web.json_response({"error": str(e)}, status=400)

    try:
        service = await get_project_service(request)
        assignment = await service.update_member_role(
            project_id=project_id,
            requester_id=user_id,
            member_user_id=member_user_id,
            old_role_id=old_role_id,
            new_role_id=new_role_id,
        )
        return web.json_response(
            {
                "user_id": str(assignment.user_id),
                "project_id": str(assignment.project_id),
                "role_id": str(assignment.role_id),
                "granted_by": str(assignment.granted_by),
                "granted_at": assignment.granted_at.isoformat(),
                "expires_at": assignment.expires_at.isoformat() if assignment.expires_at else None,
            },
            status=200,
        )
    except NotFoundError as e:
        return web.json_response({"error": str(e)}, status=404)
    except ForbiddenError as e:
        return web.json_response({"error": str(e)}, status=403)
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("Update member role error")
        return web.json_response({"error": "Internal server error"}, status=500)


def setup_routes(app: web.Application) -> None:
    """Setup project routes."""
    app.router.add_post("/projects", create_project)
    app.router.add_get("/projects", list_projects)
    app.router.add_get("/projects/{project_id}", get_project)
    app.router.add_put("/projects/{project_id}", update_project)
    app.router.add_delete("/projects/{project_id}", delete_project)
    app.router.add_get("/projects/{project_id}/members", list_members)
    app.router.add_post("/projects/{project_id}/members", add_member)
    app.router.add_delete("/projects/{project_id}/members/{user_id}", remove_member)
    app.router.add_put("/projects/{project_id}/members/{user_id}/role", update_member_role)
