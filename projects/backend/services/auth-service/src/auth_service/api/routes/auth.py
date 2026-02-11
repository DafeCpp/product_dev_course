"""Authentication routes."""
from __future__ import annotations

import structlog
from aiohttp import web

from auth_service.core.exceptions import AuthError, handle_auth_error
from backend_common.db.pool import get_pool_service as get_pool
from auth_service.domain.dto import (
    AuthTokensResponse,
    PasswordChangeRequest,
    TokenRefreshRequest,
    UserLoginRequest,
    UserRegisterRequest,
    UserResponse,
)
from auth_service.repositories.users import UserRepository
from auth_service.services.auth import AuthService

logger = structlog.get_logger(__name__)


async def get_auth_service(request: web.Request) -> AuthService:
    """Get auth service from request."""
    pool = await get_pool()
    user_repo = UserRepository(pool)
    return AuthService(user_repo)


def _extract_bearer_token(request: web.Request) -> str | None:
    """Extract Bearer token from Authorization header. Returns None if missing."""
    auth_header = request.headers.get("Authorization")
    if not auth_header or not auth_header.startswith("Bearer "):
        return None
    return auth_header[7:].strip() or None


async def register(request: web.Request) -> web.Response:
    """Register a new user."""
    try:
        data = await request.json()
        req = UserRegisterRequest(**data)
    except Exception as e:
        return web.json_response({"error": f"Invalid request: {e}"}, status=400)

    try:
        auth_service = await get_auth_service(request)
        user, tokens = await auth_service.register(
            username=req.username,
            email=req.email,
            password=req.password,
        )
        return web.json_response(
            {
                "user": UserResponse.from_user(user).model_dump(),
                "access_token": tokens.access_token,
                "refresh_token": tokens.refresh_token,
            },
            status=201,
        )
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("Registration error")
        return web.json_response({"error": "Internal server error"}, status=500)


async def login(request: web.Request) -> web.Response:
    """Login user."""
    try:
        data = await request.json()
        req = UserLoginRequest(**data)
    except Exception as e:
        return web.json_response({"error": f"Invalid request: {e}"}, status=400)

    try:
        auth_service = await get_auth_service(request)
        user, tokens = await auth_service.login(
            username=req.username,
            password=req.password,
        )
        return web.json_response(
            {
                "user": UserResponse.from_user(user).model_dump(),
                "access_token": tokens.access_token,
                "refresh_token": tokens.refresh_token,
            },
            status=200,
        )
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("Login error")
        return web.json_response({"error": "Internal server error"}, status=500)


async def refresh(request: web.Request) -> web.Response:
    """Refresh access token."""
    try:
        data = await request.json()
        req = TokenRefreshRequest(**data)
    except Exception as e:
        return web.json_response({"error": f"Invalid request: {e}"}, status=400)

    try:
        auth_service = await get_auth_service(request)
        tokens = await auth_service.refresh_token(req.refresh_token)
        return web.json_response(tokens.model_dump(), status=200)
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("Refresh error")
        return web.json_response({"error": "Internal server error"}, status=500)


async def me(request: web.Request) -> web.Response:
    """Get current user information."""
    token = _extract_bearer_token(request)
    if not token:
        return web.json_response({"error": "Unauthorized"}, status=401)

    try:
        auth_service = await get_auth_service(request)
        user = await auth_service.get_user_by_token(token)
        return web.json_response(
            UserResponse.from_user(user).model_dump(),
            status=200,
        )
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("Me error")
        return web.json_response({"error": "Internal server error"}, status=500)


async def logout(request: web.Request) -> web.Response:
    """Logout user (placeholder - in production would invalidate tokens)."""
    # TODO: Implement token blacklisting / revocation for production use.
    return web.json_response({"ok": True}, status=200)


async def change_password(request: web.Request) -> web.Response:
    """Change user password."""
    token = _extract_bearer_token(request)
    if not token:
        return web.json_response({"error": "Unauthorized"}, status=401)

    try:
        data = await request.json()
        req = PasswordChangeRequest(**data)
    except Exception as e:
        return web.json_response({"error": f"Invalid request: {e}"}, status=400)

    try:
        auth_service = await get_auth_service(request)
        # Get user from token
        user = await auth_service.get_user_by_token(token)
        # Change password
        updated_user = await auth_service.change_password(
            user.id,
            req.old_password,
            req.new_password,
        )
        return web.json_response(
            UserResponse.from_user(updated_user).model_dump(),
            status=200,
        )
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("Change password error")
        return web.json_response({"error": "Internal server error"}, status=500)


def setup_routes(app: web.Application) -> None:
    """Setup authentication routes."""
    app.router.add_post("/auth/login", login)
    app.router.add_post("/auth/register", register)
    app.router.add_post("/auth/refresh", refresh)
    app.router.add_post("/auth/logout", logout)
    app.router.add_get("/auth/me", me)
    app.router.add_post("/auth/change-password", change_password)

