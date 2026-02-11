"""Project routes."""
from __future__ import annotations

import structlog
from uuid import UUID

from aiohttp import web

from auth_service.core.exceptions import AuthError, ForbiddenError, NotFoundError, handle_auth_error
from backend_common.db.pool import get_pool_service as get_pool
from auth_service.domain.dto import (
    ProjectCreateRequest,
    ProjectMemberAddRequest,
    ProjectMemberResponse,
    ProjectMemberUpdateRequest,
    ProjectResponse,
    ProjectUpdateRequest,
)
from auth_service.repositories.projects import ProjectRepository
from auth_service.repositories.users import UserRepository
from auth_service.services.auth import AuthService
from auth_service.services.projects import ProjectService

logger = structlog.get_logger(__name__)


async def get_project_service(request: web.Request) -> ProjectService:
    """Get project service from request."""
    pool = await get_pool()
    project_repo = ProjectRepository(pool)
    user_repo = UserRepository(pool)
    return ProjectService(project_repo, user_repo)


async def get_user_id_from_token(request: web.Request) -> UUID:
    """Extract user ID from JWT token."""
    auth_header = request.headers.get("Authorization")
    if not auth_header or not auth_header.startswith("Bearer "):
        from auth_service.core.exceptions import InvalidCredentialsError
        raise InvalidCredentialsError("Unauthorized")

    token = auth_header[7:].strip()
    if not token:
        from auth_service.core.exceptions import InvalidCredentialsError
        raise InvalidCredentialsError("Unauthorized")

    pool = await get_pool()
    user_repo = UserRepository(pool)
    auth_service = AuthService(user_repo)
    user = await auth_service.get_user_by_token(token)
    return user.id


def _parse_project_id(request: web.Request) -> UUID:
    """Parse project_id from URL match info, raising 400 on invalid UUID."""
    try:
        return UUID(request.match_info["project_id"])
    except ValueError:
        raise web.HTTPBadRequest(text="Invalid project ID")


async def create_project(request: web.Request) -> web.Response:
    """Create a new project."""
    try:
        user_id = await get_user_id_from_token(request)
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
    """Get project by ID."""
    try:
        user_id = await get_user_id_from_token(request)
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
    """List all projects for current user."""
    try:
        user_id = await get_user_id_from_token(request)
    except AuthError as e:
        return handle_auth_error(request, e)

    try:
        service = await get_project_service(request)
        projects = await service.list_user_projects(user_id)
        return web.json_response(
            {
                "projects": [
                    ProjectResponse.from_project(p).model_dump()
                    for p in projects
                ]
            },
            status=200,
        )
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("List projects error")
        return web.json_response({"error": "Internal server error"}, status=500)


async def update_project(request: web.Request) -> web.Response:
    """Update project."""
    try:
        user_id = await get_user_id_from_token(request)
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
    """Delete project."""
    try:
        user_id = await get_user_id_from_token(request)
    except AuthError as e:
        return handle_auth_error(request, e)

    try:
        project_id = _parse_project_id(request)
    except web.HTTPBadRequest:
        return web.json_response({"error": "Invalid project ID"}, status=400)

    try:
        service = await get_project_service(request)
        await service.delete_project(project_id, user_id)
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
    """List project members."""
    try:
        user_id = await get_user_id_from_token(request)
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
                        role=m["role"],
                        created_at=m["created_at"].isoformat(),
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
    """Add member to project."""
    try:
        user_id = await get_user_id_from_token(request)
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
    except ValueError:
        return web.json_response({"error": "Invalid user ID"}, status=400)

    try:
        service = await get_project_service(request)
        member = await service.add_member(project_id, user_id, new_user_id, req.role)
        return web.json_response(
            ProjectMemberResponse(
                project_id=str(member.project_id),
                user_id=str(member.user_id),
                role=member.role,
                created_at=member.created_at.isoformat(),
            ).model_dump(),
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
    """Remove member from project."""
    try:
        user_id = await get_user_id_from_token(request)
    except AuthError as e:
        return handle_auth_error(request, e)

    try:
        project_id = UUID(request.match_info["project_id"])
        member_user_id = UUID(request.match_info["user_id"])
    except ValueError:
        return web.json_response({"error": "Invalid ID"}, status=400)

    try:
        service = await get_project_service(request)
        await service.remove_member(project_id, user_id, member_user_id)
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
    """Update member role."""
    try:
        user_id = await get_user_id_from_token(request)
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
        service = await get_project_service(request)
        member = await service.update_member_role(project_id, user_id, member_user_id, req.role)
        return web.json_response(
            ProjectMemberResponse(
                project_id=str(member.project_id),
                user_id=str(member.user_id),
                role=member.role,
                created_at=member.created_at.isoformat(),
            ).model_dump(),
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

