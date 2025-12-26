"""Authentication service."""
from __future__ import annotations

from uuid import UUID

from auth_service.core.exceptions import (
    InvalidCredentialsError,
    UserAlreadyExistsError,
    UserNotFoundError,
)
from auth_service.domain.dto import AuthTokensResponse, UserResponse
from auth_service.domain.models import User
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

    def __init__(self, user_repository: UserRepository):
        self._user_repo = user_repository

    async def register(
        self,
        username: str,
        email: str,
        password: str,
    ) -> tuple[User, AuthTokensResponse]:
        """Register a new user."""
        # Check if user already exists
        if await self._user_repo.user_exists(username, email):
            raise UserAlreadyExistsError("User with this username or email already exists")

        # Hash password
        hashed_password = hash_password(password)

        # Create user (password_change_required defaults to False for new registrations)
        user = await self._user_repo.create(username, email, hashed_password, password_change_required=False)

        # Generate tokens
        tokens = self._create_tokens(str(user.id))

        return user, tokens

    async def login(self, username: str, password: str) -> tuple[User, AuthTokensResponse]:
        """Authenticate user and return tokens."""
        # Get user by username
        user = await self._user_repo.get_by_username(username)
        if not user:
            raise InvalidCredentialsError()

        # Verify password
        if not verify_password(password, user.hashed_password):
            raise InvalidCredentialsError()

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

    async def refresh_token(self, refresh_token: str) -> AuthTokensResponse:
        """Refresh access token using refresh token."""
        try:
            payload = decode_token(refresh_token)
            if payload.get("type") != "refresh":
                raise ValueError("Invalid token type")
            user_id = payload.get("sub")
            if not user_id:
                raise ValueError("Token missing user ID")
        except ValueError as e:
            raise InvalidCredentialsError(str(e)) from e

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

        return user

    def _create_tokens(self, user_id: str) -> AuthTokensResponse:
        """Create access and refresh tokens."""
        return AuthTokensResponse(
            access_token=create_access_token(user_id),
            refresh_token=create_refresh_token(user_id),
        )

