"""User search routes."""
from __future__ import annotations

from uuid import UUID

import structlog
from aiohttp import web

from auth_service.core.exceptions import AuthError, handle_auth_error
from auth_service.domain.dto import UserSearchResult
from auth_service.repositories.users import UserRepository
from auth_service.api.utils import get_requester_id
from auth_service.services.dependencies import get_permission_service
from backend_common.db.pool import get_pool_service as get_pool

logger = structlog.get_logger(__name__)

_MAX_LIMIT = 10
_MIN_QUERY_LENGTH = 2


async def search_users(request: web.Request) -> web.Response:
    """Search active users by username prefix for autocomplete.

    GET /api/v1/users/search?q=<query>[&exclude_project_id=<uuid>][&limit=10]

    Requires: valid Bearer JWT token.
    """
    try:
        perm_svc = await get_permission_service(request)
        await get_requester_id(request, perm_svc)
    except AuthError as exc:
        return handle_auth_error(request, exc)

    q = request.rel_url.query.get("q", "").strip()
    if len(q) < _MIN_QUERY_LENGTH:
        return web.json_response(
            {"error": f"Query must be at least {_MIN_QUERY_LENGTH} characters"},
            status=400,
        )

    raw_limit = request.rel_url.query.get("limit", str(_MAX_LIMIT))
    try:
        limit = min(int(raw_limit), _MAX_LIMIT)
    except ValueError:
        limit = _MAX_LIMIT

    exclude_project_id: UUID | None = None
    raw_project_id = request.rel_url.query.get("exclude_project_id")
    if raw_project_id:
        try:
            exclude_project_id = UUID(raw_project_id)
        except ValueError:
            return web.json_response({"error": "Invalid exclude_project_id"}, status=400)

    try:
        pool = await get_pool()
        user_repo = UserRepository(pool)
        results = await user_repo.search_by_username(
            query=q,
            limit=limit,
            exclude_project_id=exclude_project_id,
        )
        return web.json_response(
            [UserSearchResult(id=r["id"], username=r["username"], email=r["email"], is_active=r["is_active"]).model_dump() for r in results],
            status=200,
        )
    except Exception:
        logger.exception("User search error")
        return web.json_response({"error": "Internal server error"}, status=500)


def setup_routes(app: web.Application) -> None:
    """Register user search routes."""
    app.router.add_get("/api/v1/users/search", search_users)
