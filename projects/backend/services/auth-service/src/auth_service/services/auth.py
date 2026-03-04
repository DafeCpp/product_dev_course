"""Authentication service."""
from __future__ import annotations

import secrets
from datetime import datetime, timedelta, timezone
from uuid import UUID

from auth_service.core.exceptions import (
    ConflictError,
    ForbiddenError,
    InvalidCredentialsError,
    InvalidTokenError,
    NotFoundError,
    UserAlreadyExistsError,
    UserNotFoundError,
)
from auth_service.domain.dto import AuthTokensResponse, UserResponse
from auth_service.domain.models import InviteToken, User
from auth_service.repositories.invites import InviteRepository
from auth_service.repositories.password_reset import PasswordResetRepository
from auth_service.repositories.revoked_tokens import RevokedTokenRepository
from auth_service.repositories.users import UserRepository
from auth_service.services.jwt import (
    create_access_token,
    create_refresh_token,
    decode_token,
    get_user_id_from_token,
)
from auth_service.services.password import hash_password, verify_password


class AuthService:
    """Service for authentication operations."""

    def __init__(
        self,
        user_repository: UserRepository,
        revoked_repo: RevokedTokenRepository,
        reset_repo: PasswordResetRepository,
        invite_repo: InviteRepository | None = None,
        registration_mode: str = "open",
    ):
        self._user_repo = user_repository
        self._revoked_repo = revoked_repo
        self._reset_repo = reset_repo
        self._invite_repo = invite_repo
        self._registration_mode = registration_mode

    async def bootstrap_admin(
        self,
        bootstrap_secret: str,
        expected_secret: str | None,
        username: str,
        email: str,
        password: str,
    ) -> tuple[User, AuthTokensResponse]:
        """Создаёт первого admin-пользователя. Работает только если ни одного admin ещё нет."""
        if not expected_secret or bootstrap_secret != expected_secret:
            raise ForbiddenError("Invalid bootstrap secret")

        if await self._user_repo.count_admins() > 0:
            raise ConflictError("Admin user already exists")

        if await self._user_repo.user_exists(username, email):
            raise UserAlreadyExistsError("User with this username or email already exists")

        hashed_password = hash_password(password)
        user = await self._user_repo.create(username, email, hashed_password, password_change_required=False)
        user = await self._user_repo.set_admin(user.id, True)

        tokens = self._create_tokens(str(user.id))
        return user, tokens

    async def register(
        self,
        username: str,
        email: str,
        password: str,
        invite_token: UUID | None = None,
    ) -> tuple[User, AuthTokensResponse]:
        """Register a new user."""
        # Проверка инвайт-режима
        if self._registration_mode == "invite":
            if self._invite_repo is None:
                raise ForbiddenError("Invite system is not configured")
            if invite_token is None:
                raise ForbiddenError("Invite token required")
            invite = await self._invite_repo.get_by_token(invite_token)
            if not invite or not invite.is_active:
                raise InvalidTokenError("Invalid or expired invite token")

        # Check if user already exists
        if await self._user_repo.user_exists(username, email):
            raise UserAlreadyExistsError("User with this username or email already exists")

        # Hash password
        hashed_password = hash_password(password)

        # Create user (password_change_required defaults to False for new registrations)
        user = await self._user_repo.create(username, email, hashed_password, password_change_required=False)

        # Mark invite as used (only in invite mode)
        if self._registration_mode == "invite" and self._invite_repo is not None and invite_token is not None:
            await self._invite_repo.mark_used(invite_token, user.id)

        # Generate tokens
        tokens = self._create_tokens(str(user.id))

        return user, tokens

    async def create_invite(
        self,
        requester_token: str,
        email_hint: str | None,
        expires_in_hours: int,
    ) -> InviteToken:
        """Create a new invite token. Requires admin privileges."""
        requester = await self.get_user_by_token(requester_token)
        if not requester.is_admin:
            raise ForbiddenError()
        if self._invite_repo is None:
            raise ForbiddenError("Invite system is not configured")

        expires_at = datetime.now(timezone.utc) + timedelta(hours=expires_in_hours)
        return await self._invite_repo.create(requester.id, email_hint, expires_at)

    async def list_invites(
        self,
        requester_token: str,
        active_only: bool = False,
    ) -> list[InviteToken]:
        """List invite tokens. Requires admin privileges."""
        requester = await self.get_user_by_token(requester_token)
        if not requester.is_admin:
            raise ForbiddenError()
        if self._invite_repo is None:
            raise ForbiddenError("Invite system is not configured")

        return await self._invite_repo.list_all(active_only=active_only)

    async def revoke_invite(self, requester_token: str, token: UUID) -> bool:
        """Revoke (delete) an invite token. Requires admin. Cannot revoke used tokens."""
        requester = await self.get_user_by_token(requester_token)
        if not requester.is_admin:
            raise ForbiddenError()
        if self._invite_repo is None:
            raise ForbiddenError("Invite system is not configured")

        invite = await self._invite_repo.get_by_token(token)
        if not invite:
            raise NotFoundError("Invite token not found")
        if invite.used_at is not None:
            raise ConflictError("Cannot revoke a used invite token")

        return await self._invite_repo.delete(token)

    async def login(self, username: str, password: str) -> tuple[User, AuthTokensResponse]:
        """Authenticate user and return tokens."""
        # Get user by username
        user = await self._user_repo.get_by_username(username)
        if not user:
            raise InvalidCredentialsError()

        # Verify password
        if not verify_password(password, user.hashed_password):
            raise InvalidCredentialsError()

        # Проверка активности аккаунта
        if not user.is_active:
            raise ForbiddenError("Account is deactivated")

        # Generate tokens
        tokens = self._create_tokens(str(user.id))

        return user, tokens

    async def change_password(
        self,
        user_id: UUID,
        old_password: str,
        new_password: str,
    ) -> User:
        """Change user password."""
        # Get user
        user = await self._user_repo.get_by_id(user_id)
        if not user:
            raise UserNotFoundError()

        # Verify old password
        if not verify_password(old_password, user.hashed_password):
            raise InvalidCredentialsError("Invalid old password")

        # Hash new password
        new_hashed_password = hash_password(new_password)

        # Update password and clear password_change_required flag
        updated_user = await self._user_repo.update_password(
            user_id,
            new_hashed_password,
            password_change_required=False,
        )

        return updated_user

    async def logout(self, refresh_token: str) -> None:
        """Revoke a refresh token."""
        try:
            payload = decode_token(refresh_token)
            if payload.get("type") != "refresh":
                raise ValueError("Invalid token type")
            jti = payload.get("jti")
            if not jti:
                raise ValueError("Token missing jti")
            user_id = payload.get("sub")
            if not user_id:
                raise ValueError("Token missing user ID")
            exp: int = payload["exp"]
        except ValueError as e:
            raise InvalidCredentialsError(str(e)) from e

        expires_at = datetime.fromtimestamp(exp, tz=timezone.utc)
        await self._revoked_repo.revoke(UUID(jti), UUID(user_id), expires_at)

    async def refresh_token(self, refresh_token: str) -> AuthTokensResponse:
        """Refresh access token using refresh token."""
        try:
            payload = decode_token(refresh_token)
            if payload.get("type") != "refresh":
                raise ValueError("Invalid token type")
            jti = payload.get("jti")
            if not jti:
                raise ValueError("Token missing jti")
            user_id = payload.get("sub")
            if not user_id:
                raise ValueError("Token missing user ID")
        except ValueError as e:
            raise InvalidCredentialsError(str(e)) from e

        # Проверяем blacklist
        if await self._revoked_repo.is_revoked(UUID(jti)):
            raise InvalidCredentialsError("Token has been revoked")

        # Verify user exists
        user = await self._user_repo.get_by_id(UUID(user_id))
        if not user:
            raise UserNotFoundError()

        # Generate new tokens
        return self._create_tokens(user_id)

    async def get_user_by_token(self, access_token: str) -> User:
        """Get user from access token."""
        try:
            user_id = get_user_id_from_token(access_token)
        except ValueError as e:
            raise InvalidCredentialsError(str(e)) from e

        user = await self._user_repo.get_by_id(UUID(user_id))
        if not user:
            raise UserNotFoundError()

        if not user.is_active:
            raise ForbiddenError("Account is deactivated")

        return user

    async def request_password_reset(self, email: str) -> tuple[str, datetime]:
        """Generate a password reset token for the given email."""
        user = await self._user_repo.get_by_email(email)
        if not user:
            raise UserNotFoundError()

        token = secrets.token_urlsafe(32)
        expires_at = datetime.now(timezone.utc) + timedelta(hours=1)
        await self._reset_repo.create_token(token, user.id, expires_at)
        return token, expires_at

    async def confirm_password_reset(self, token: str, new_password: str) -> AuthTokensResponse:
        """Reset password using a valid reset token."""
        record = await self._reset_repo.get_by_token(token)
        if not record:
            raise InvalidCredentialsError("Invalid or expired reset token")

        if record["expires_at"] < datetime.now(timezone.utc):
            raise InvalidCredentialsError("Reset token expired")

        user_id: UUID = record["user_id"]
        await self._user_repo.update_password(user_id, hash_password(new_password), password_change_required=False)
        await self._reset_repo.delete_token(token)
        return self._create_tokens(str(user_id))

    async def admin_reset_user(
        self,
        requester_token: str,
        target_user_id: UUID,
        new_password: str | None,
    ) -> tuple[User, str]:
        """Admin resets another user's password."""
        requester = await self.get_user_by_token(requester_token)
        if not requester.is_admin:
            raise ForbiddenError()

        target = await self._user_repo.get_by_id(target_user_id)
        if not target:
            raise UserNotFoundError()

        pwd = new_password if new_password else ("Tmp1" + secrets.token_hex(8))
        updated_user = await self._user_repo.update_password(
            target_user_id,
            hash_password(pwd),
            password_change_required=True,
        )
        return updated_user, pwd

    async def list_users(
        self,
        requester_token: str,
        search: str | None = None,
    ) -> list[User]:
        """List all users. Requires admin privileges."""
        requester = await self.get_user_by_token(requester_token)
        if not requester.is_admin:
            raise ForbiddenError()
        return await self._user_repo.list_all(search)

    async def update_user(
        self,
        requester_token: str,
        target_user_id: UUID,
        is_active: bool | None,
        is_admin: bool | None,
    ) -> User:
        """Update user is_active/is_admin flags. Requires admin privileges."""
        requester = await self.get_user_by_token(requester_token)
        if not requester.is_admin:
            raise ForbiddenError()

        if requester.id == target_user_id:
            raise ForbiddenError("Cannot modify your own account")

        target = await self._user_repo.get_by_id(target_user_id)
        if not target:
            raise UserNotFoundError()

        # Защита от разжалования последнего admin'а
        if is_admin is False and target.is_admin:
            if await self._user_repo.count_admins() < 2:
                raise ConflictError("Cannot remove the last admin")

        if is_active is not None:
            target = await self._user_repo.set_active(target_user_id, is_active)
        if is_admin is not None:
            target = await self._user_repo.set_admin(target_user_id, is_admin)

        return target

    async def delete_user(
        self,
        requester_token: str,
        target_user_id: UUID,
    ) -> bool:
        """Delete a user. Requires admin privileges."""
        requester = await self.get_user_by_token(requester_token)
        if not requester.is_admin:
            raise ForbiddenError()

        if requester.id == target_user_id:
            raise ForbiddenError("Cannot delete your own account")

        target = await self._user_repo.get_by_id(target_user_id)
        if not target:
            raise NotFoundError("User not found")

        if target.is_admin:
            if await self._user_repo.count_admins() < 2:
                raise ConflictError("Cannot delete the last admin")

        deleted = await self._user_repo.delete(target_user_id)
        if not deleted:
            raise NotFoundError("User not found")
        return True

    def _create_tokens(self, user_id: str) -> AuthTokensResponse:
        """Create access and refresh tokens."""
        return AuthTokensResponse(
            access_token=create_access_token(user_id),
            refresh_token=create_refresh_token(user_id),
        )

