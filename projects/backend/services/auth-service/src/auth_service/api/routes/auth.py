"""Authentication routes."""
from __future__ import annotations

from aiohttp import web

from auth_service.core.exceptions import AuthError, handle_auth_error
from auth_service.db.pool import get_pool
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


def get_auth_service(request: web.Request) -> AuthService:
    """Get auth service from request."""
    pool = get_pool()
    user_repo = UserRepository(pool)
    return AuthService(user_repo)


async def register(request: web.Request) -> web.Response:
    """Register a new user."""
    try:
        data = await request.json()
        req = UserRegisterRequest(**data)
    except Exception as e:
        return web.json_response({"error": f"Invalid request: {e}"}, status=400)

    try:
        auth_service = get_auth_service(request)
        user, tokens = await auth_service.register(
            username=req.username,
            email=req.email,
            password=req.password,
        )
        return web.json_response(
            {
                "user": UserResponse(
                    id=str(user.id),
                    username=user.username,
                    email=user.email,
                    password_change_required=user.password_change_required,
                ).model_dump(),
                "access_token": tokens.access_token,
                "refresh_token": tokens.refresh_token,
            },
            status=201,
        )
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception as e:
        request.app.logger.error(f"Registration error: {e}")  # type: ignore
        return web.json_response({"error": "Internal server error"}, status=500)


async def login(request: web.Request) -> web.Response:
    """Login user."""
    try:
        data = await request.json()
        req = UserLoginRequest(**data)
    except Exception as e:
        return web.json_response({"error": f"Invalid request: {e}"}, status=400)

    try:
        auth_service = get_auth_service(request)
        user, tokens = await auth_service.login(
            username=req.username,
            password=req.password,
        )
        return web.json_response(
            {
                "user": UserResponse(
                    id=str(user.id),
                    username=user.username,
                    email=user.email,
                    password_change_required=user.password_change_required,
                ).model_dump(),
                "access_token": tokens.access_token,
                "refresh_token": tokens.refresh_token,
            },
            status=200,
        )
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception as e:
        request.app.logger.error(f"Login error: {e}")  # type: ignore
        return web.json_response({"error": "Internal server error"}, status=500)


async def refresh(request: web.Request) -> web.Response:
    """Refresh access token."""
    try:
        data = await request.json()
        req = TokenRefreshRequest(**data)
    except Exception as e:
        return web.json_response({"error": f"Invalid request: {e}"}, status=400)

    try:
        auth_service = get_auth_service(request)
        tokens = await auth_service.refresh_token(req.refresh_token)
        return web.json_response(tokens.model_dump(), status=200)
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception as e:
        request.app.logger.error(f"Refresh error: {e}")  # type: ignore
        return web.json_response({"error": "Internal server error"}, status=500)


async def me(request: web.Request) -> web.Response:
    """Get current user information."""
    auth_header = request.headers.get("Authorization")
    if not auth_header or not auth_header.startswith("Bearer "):
        return web.json_response({"error": "Unauthorized"}, status=401)

    token = auth_header[7:]  # Remove "Bearer "

    try:
        auth_service = get_auth_service(request)
        user = await auth_service.get_user_by_token(token)
        return web.json_response(
            UserResponse(
                id=str(user.id),
                username=user.username,
                email=user.email,
                password_change_required=user.password_change_required,
            ).model_dump(),
            status=200,
        )
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception as e:
        request.app.logger.error(f"Me error: {e}")  # type: ignore
        return web.json_response({"error": "Internal server error"}, status=500)


async def logout(request: web.Request) -> web.Response:
    """Logout user (placeholder - in production would invalidate tokens)."""
    return web.json_response({"ok": True}, status=200)


async def change_password(request: web.Request) -> web.Response:
    """Change user password."""
    auth_header = request.headers.get("Authorization")
    if not auth_header or not auth_header.startswith("Bearer "):
        return web.json_response({"error": "Unauthorized"}, status=401)

    token = auth_header[7:]  # Remove "Bearer "

    try:
        data = await request.json()
        req = PasswordChangeRequest(**data)
    except Exception as e:
        return web.json_response({"error": f"Invalid request: {e}"}, status=400)

    try:
        auth_service = get_auth_service(request)
        # Get user from token
        user = await auth_service.get_user_by_token(token)
        # Change password
        updated_user = await auth_service.change_password(
            user.id,
            req.old_password,
            req.new_password,
        )
        return web.json_response(
            UserResponse(
                id=str(updated_user.id),
                username=updated_user.username,
                email=updated_user.email,
                password_change_required=updated_user.password_change_required,
            ).model_dump(),
            status=200,
        )
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception as e:
        request.app.logger.error(f"Change password error: {e}")  # type: ignore
        return web.json_response({"error": "Internal server error"}, status=500)


def setup_routes(app: web.Application) -> None:
    """Setup authentication routes."""
    app.router.add_post("/auth/login", login)
    app.router.add_post("/auth/register", register)
    app.router.add_post("/auth/refresh", refresh)
    app.router.add_post("/auth/logout", logout)
    app.router.add_get("/auth/me", me)
    app.router.add_post("/auth/change-password", change_password)

