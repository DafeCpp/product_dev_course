"""Authentication routes."""
from __future__ import annotations

from uuid import UUID

import structlog
from aiohttp import web

from auth_service.core.exceptions import AuthError, handle_auth_error
from backend_common.db.pool import get_pool_service as get_pool
from auth_service.domain.dto import (
    AdminUserResetRequest,
    AdminUserUpdateRequest,
    AuthTokensResponse,
    BootstrapAdminRequest,
    InviteCreateRequest,
    InviteResponse,
    LogoutRequest,
    PasswordChangeRequest,
    PasswordResetConfirmRequest,
    PasswordResetRequestDto,
    TokenRefreshRequest,
    UserLoginRequest,
    UserRegisterRequest,
    UserResponse,
)
from auth_service.repositories.invites import InviteRepository
from auth_service.repositories.password_reset import PasswordResetRepository
from auth_service.repositories.revoked_tokens import RevokedTokenRepository
from auth_service.repositories.users import UserRepository
from auth_service.services.auth import AuthService
from auth_service.settings import settings

logger = structlog.get_logger(__name__)


async def get_auth_service(request: web.Request) -> AuthService:
    """Get auth service from request."""
    pool = await get_pool()
    user_repo = UserRepository(pool)
    revoked_repo = RevokedTokenRepository(pool)
    reset_repo = PasswordResetRepository(pool)
    invite_repo = InviteRepository(pool)
    return AuthService(
        user_repo,
        revoked_repo,
        reset_repo,
        invite_repo=invite_repo,
        registration_mode=settings.registration_mode,
    )


def _extract_bearer_token(request: web.Request) -> str | None:
    """Extract Bearer token from Authorization header. Returns None if missing."""
    auth_header = request.headers.get("Authorization")
    if not auth_header or not auth_header.startswith("Bearer "):
        return None
    return auth_header[7:].strip() or None


async def bootstrap_admin(request: web.Request) -> web.Response:
    """Создаёт первого admin-пользователя. Требует ADMIN_BOOTSTRAP_SECRET. Только пока нет ни одного admin."""
    if not settings.admin_bootstrap_secret:
        return web.json_response({"error": "Bootstrap is disabled"}, status=404)

    try:
        data = await request.json()
        req = BootstrapAdminRequest(**data)
    except Exception as e:
        return web.json_response({"error": f"Invalid request: {e}"}, status=400)

    try:
        auth_service = await get_auth_service(request)
        user, tokens = await auth_service.bootstrap_admin(
            bootstrap_secret=req.bootstrap_secret,
            expected_secret=settings.admin_bootstrap_secret,
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
        logger.exception("Bootstrap admin error")
        return web.json_response({"error": "Internal server error"}, status=500)


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
            invite_token=req.invite_token,
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
    """Logout user — revokes the provided refresh token."""
    try:
        data = await request.json()
        req = LogoutRequest(**data)
    except Exception as e:
        return web.json_response({"error": f"Invalid request: {e}"}, status=400)

    try:
        auth_service = await get_auth_service(request)
        await auth_service.logout(req.refresh_token)
        return web.json_response({"ok": True}, status=200)
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("Logout error")
        return web.json_response({"error": "Internal server error"}, status=500)


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


async def password_reset_request(request: web.Request) -> web.Response:
    """Request a password reset token."""
    try:
        data = await request.json()
        req = PasswordResetRequestDto(**data)
    except Exception as e:
        return web.json_response({"error": f"Invalid request: {e}"}, status=400)

    try:
        auth_service = await get_auth_service(request)
        token, expires_at = await auth_service.request_password_reset(req.email)
        return web.json_response(
            {"reset_token": token, "expires_at": expires_at.isoformat()},
            status=200,
        )
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("Password reset request error")
        return web.json_response({"error": "Internal server error"}, status=500)


async def password_reset_confirm(request: web.Request) -> web.Response:
    """Confirm a password reset using a token."""
    try:
        data = await request.json()
        req = PasswordResetConfirmRequest(**data)
    except Exception as e:
        return web.json_response({"error": f"Invalid request: {e}"}, status=400)

    try:
        auth_service = await get_auth_service(request)
        tokens = await auth_service.confirm_password_reset(req.reset_token, req.new_password)
        return web.json_response(tokens.model_dump(), status=200)
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("Password reset confirm error")
        return web.json_response({"error": "Internal server error"}, status=500)


async def admin_reset_user(request: web.Request) -> web.Response:
    """Admin resets another user's password."""
    token = _extract_bearer_token(request)
    if not token:
        return web.json_response({"error": "Unauthorized"}, status=401)

    user_id_str = request.match_info.get("user_id", "")

    try:
        data = await request.json()
        req = AdminUserResetRequest(**data)
    except Exception as e:
        return web.json_response({"error": f"Invalid request: {e}"}, status=400)

    try:
        from uuid import UUID

        target_user_id = UUID(user_id_str)
    except ValueError:
        return web.json_response({"error": "Invalid user_id"}, status=400)

    try:
        auth_service = await get_auth_service(request)
        updated_user, new_password = await auth_service.admin_reset_user(token, target_user_id, req.new_password)
        return web.json_response(
            {"user": UserResponse.from_user(updated_user).model_dump(), "new_password": new_password},
            status=200,
        )
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("Admin reset user error")
        return web.json_response({"error": "Internal server error"}, status=500)


async def create_invite(request: web.Request) -> web.Response:
    """Create a new invite token (admin only)."""
    token = _extract_bearer_token(request)
    if not token:
        return web.json_response({"error": "Unauthorized"}, status=401)

    try:
        data = await request.json()
        req = InviteCreateRequest(**data)
    except Exception as e:
        return web.json_response({"error": f"Invalid request: {e}"}, status=400)

    try:
        auth_service = await get_auth_service(request)
        invite = await auth_service.create_invite(token, req.email_hint, req.expires_in_hours)
        return web.json_response(InviteResponse.from_model(invite).model_dump(mode="json"), status=201)
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("Create invite error")
        return web.json_response({"error": "Internal server error"}, status=500)


async def list_invites(request: web.Request) -> web.Response:
    """List all invite tokens (admin only)."""
    token = _extract_bearer_token(request)
    if not token:
        return web.json_response({"error": "Unauthorized"}, status=401)

    active_only_str = request.rel_url.query.get("active_only", "false").lower()
    active_only = active_only_str in ("true", "1", "yes")

    try:
        auth_service = await get_auth_service(request)
        invites = await auth_service.list_invites(token, active_only=active_only)
        return web.json_response(
            [InviteResponse.from_model(inv).model_dump(mode="json") for inv in invites],
            status=200,
        )
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("List invites error")
        return web.json_response({"error": "Internal server error"}, status=500)


async def revoke_invite(request: web.Request) -> web.Response:
    """Revoke (delete) an invite token (admin only)."""
    token = _extract_bearer_token(request)
    if not token:
        return web.json_response({"error": "Unauthorized"}, status=401)

    token_str = request.match_info.get("token", "")
    try:
        invite_token_uuid = UUID(token_str)
    except ValueError:
        return web.json_response({"error": "Invalid token format"}, status=400)

    try:
        auth_service = await get_auth_service(request)
        await auth_service.revoke_invite(token, invite_token_uuid)
        return web.Response(status=204)
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("Revoke invite error")
        return web.json_response({"error": "Internal server error"}, status=500)


async def list_users(request: web.Request) -> web.Response:
    """List all users (admin only)."""
    token = _extract_bearer_token(request)
    if not token:
        return web.json_response({"error": "Unauthorized"}, status=401)

    search = request.rel_url.query.get("search") or None
    is_active_str = request.rel_url.query.get("is_active")

    try:
        auth_service = await get_auth_service(request)
        users = await auth_service.list_users(token, search=search)
        # Фильтр по is_active применяем в Python
        if is_active_str is not None:
            filter_active = is_active_str.lower() in ("true", "1", "yes")
            users = [u for u in users if u.is_active == filter_active]
        return web.json_response(
            [UserResponse.from_user(u).model_dump() for u in users],
            status=200,
        )
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("List users error")
        return web.json_response({"error": "Internal server error"}, status=500)


async def update_user(request: web.Request) -> web.Response:
    """Update user flags (admin only)."""
    token = _extract_bearer_token(request)
    if not token:
        return web.json_response({"error": "Unauthorized"}, status=401)

    user_id_str = request.match_info.get("user_id", "")
    try:
        target_user_id = UUID(user_id_str)
    except ValueError:
        return web.json_response({"error": "Invalid user_id"}, status=400)

    try:
        data = await request.json()
        req = AdminUserUpdateRequest(**data)
    except Exception as e:
        return web.json_response({"error": f"Invalid request: {e}"}, status=400)

    try:
        auth_service = await get_auth_service(request)
        user = await auth_service.update_user(token, target_user_id, req.is_active, req.is_admin)
        return web.json_response(UserResponse.from_user(user).model_dump(), status=200)
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("Update user error")
        return web.json_response({"error": "Internal server error"}, status=500)


async def delete_user(request: web.Request) -> web.Response:
    """Delete user (admin only)."""
    token = _extract_bearer_token(request)
    if not token:
        return web.json_response({"error": "Unauthorized"}, status=401)

    user_id_str = request.match_info.get("user_id", "")
    try:
        target_user_id = UUID(user_id_str)
    except ValueError:
        return web.json_response({"error": "Invalid user_id"}, status=400)

    try:
        auth_service = await get_auth_service(request)
        await auth_service.delete_user(token, target_user_id)
        return web.Response(status=204)
    except AuthError as e:
        return handle_auth_error(request, e)
    except Exception:
        logger.exception("Delete user error")
        return web.json_response({"error": "Internal server error"}, status=500)


def setup_routes(app: web.Application) -> None:
    """Setup authentication routes."""
    app.router.add_post("/auth/admin/bootstrap", bootstrap_admin)
    app.router.add_post("/auth/login", login)
    app.router.add_post("/auth/register", register)
    app.router.add_post("/auth/refresh", refresh)
    app.router.add_post("/auth/logout", logout)
    app.router.add_get("/auth/me", me)
    app.router.add_post("/auth/change-password", change_password)
    app.router.add_post("/auth/password-reset/request", password_reset_request)
    app.router.add_post("/auth/password-reset/confirm", password_reset_confirm)
    app.router.add_post("/auth/admin/users/{user_id}/reset", admin_reset_user)
    app.router.add_post("/auth/admin/invites", create_invite)
    app.router.add_get("/auth/admin/invites", list_invites)
    app.router.add_delete("/auth/admin/invites/{token}", revoke_invite)
    app.router.add_get("/auth/admin/users", list_users)
    app.router.add_patch("/auth/admin/users/{user_id}", update_user)
    app.router.add_delete("/auth/admin/users/{user_id}", delete_user)

