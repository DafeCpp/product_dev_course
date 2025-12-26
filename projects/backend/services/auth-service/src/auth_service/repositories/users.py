"""User repository."""
from __future__ import annotations

from uuid import UUID

import asyncpg  # type: ignore[import-untyped]

from auth_service.domain.models import User
from auth_service.repositories.base import BaseRepository


class UserRepository(BaseRepository):
    """Repository for user operations."""

    async def create(
        self,
        username: str,
        email: str,
        hashed_password: str,
        password_change_required: bool = False,
    ) -> User:
        """Create a new user."""
        query = """
            INSERT INTO users (username, email, hashed_password, password_change_required)
            VALUES ($1, $2, $3, $4)
            RETURNING id, username, email, hashed_password, password_change_required, created_at, updated_at
        """
        row = await self._fetchrow(query, username, email, hashed_password, password_change_required)
        if not row:
            raise RuntimeError("Failed to create user")
        return User.from_row(dict(row))

    async def get_by_id(self, user_id: UUID) -> User | None:
        """Get user by ID."""
        query = """
            SELECT id, username, email, hashed_password, password_change_required, created_at, updated_at
            FROM users
            WHERE id = $1
        """
        row = await self._fetchrow(query, user_id)
        if not row:
            return None
        return User.from_row(dict(row))

    async def get_by_username(self, username: str) -> User | None:
        """Get user by username."""
        query = """
            SELECT id, username, email, hashed_password, password_change_required, created_at, updated_at
            FROM users
            WHERE username = $1
        """
        row = await self._fetchrow(query, username)
        if not row:
            return None
        return User.from_row(dict(row))

    async def get_by_email(self, email: str) -> User | None:
        """Get user by email."""
        query = """
            SELECT id, username, email, hashed_password, password_change_required, created_at, updated_at
            FROM users
            WHERE email = $1
        """
        row = await self._fetchrow(query, email)
        if not row:
            return None
        return User.from_row(dict(row))

    async def update_password(
        self,
        user_id: UUID,
        new_hashed_password: str,
        password_change_required: bool = False,
    ) -> User:
        """Update user password."""
        query = """
            UPDATE users
            SET hashed_password = $2,
                password_change_required = $3,
                updated_at = now()
            WHERE id = $1
            RETURNING id, username, email, hashed_password, password_change_required, created_at, updated_at
        """
        row = await self._fetchrow(query, user_id, new_hashed_password, password_change_required)
        if not row:
            raise RuntimeError("Failed to update password")
        return User.from_row(dict(row))

    async def user_exists(self, username: str, email: str) -> bool:
        """Check if user with username or email exists."""
        query = """
            SELECT EXISTS(
                SELECT 1 FROM users
                WHERE username = $1 OR email = $2
            )
        """
        row = await self._fetchrow(query, username, email)
        return bool(row["exists"]) if row else False

