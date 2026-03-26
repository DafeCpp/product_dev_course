"""Shared API utilities."""
from __future__ import annotations

from uuid import UUID

from aiohttp import web


def extract_client_ip(request: web.Request) -> str | None:
    """Extract client IP address, respecting X-Forwarded-For."""
    forwarded_for = request.headers.get("X-Forwarded-For")
    if forwarded_for:
        return forwarded_for.split(",")[0].strip()
    return request.remote or None


def extract_user_agent(request: web.Request) -> str | None:
    """Extract User-Agent header."""
    return request.headers.get("User-Agent") or None

from auth_service.core.exceptions import InvalidCredentialsError
from auth_service.repositories.users import UserRepository
from auth_service.services.jwt import get_user_id_from_token
from auth_service.services.permission import PermissionService
from backend_common.db.pool import get_pool_service as get_pool


def extract_bearer_token(request: web.Request) -> str | None:
    """Extract Bearer token from Authorization header."""
    auth_header = request.headers.get("Authorization")
    if not auth_header or not auth_header.startswith("Bearer "):
        return None
    return auth_header[7:].strip() or None


async def get_requester_id(request: web.Request, perm_svc: PermissionService) -> UUID:
    """Extract and validate requester user ID from Bearer token.

    Raises InvalidCredentialsError if token is missing, invalid, or the user
    does not exist / is inactive.
    """
    token = extract_bearer_token(request)
    if not token:
        raise InvalidCredentialsError("Unauthorized")

    user_id = UUID(get_user_id_from_token(token))

    pool = await get_pool()
    user = await UserRepository(pool).get_by_id(user_id)
    if not user or not user.is_active:
        raise InvalidCredentialsError("User not found or inactive")

    return user_id
