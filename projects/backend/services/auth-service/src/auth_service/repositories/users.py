"""User repository."""
from __future__ import annotations

from uuid import UUID

import asyncpg  # type: ignore[import-untyped]

from auth_service.domain.models import User
from auth_service.repositories.base import BaseRepository

_SELECT_COLS = "id, username, email, hashed_password, password_change_required, is_admin, is_active, created_at, updated_at"


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
        query = f"""
            INSERT INTO users (username, email, hashed_password, password_change_required)
            VALUES ($1, $2, $3, $4)
            RETURNING {_SELECT_COLS}
        """
        row = await self._fetchrow(query, username, email, hashed_password, password_change_required)
        if not row:
            raise RuntimeError("Failed to create user")
        return User.from_row(dict(row))

    async def get_by_id(self, user_id: UUID) -> User | None:
        """Get user by ID."""
        query = f"SELECT {_SELECT_COLS} FROM users WHERE id = $1"
        row = await self._fetchrow(query, user_id)
        if not row:
            return None
        return User.from_row(dict(row))

    async def get_by_username(self, username: str) -> User | None:
        """Get user by username."""
        query = f"SELECT {_SELECT_COLS} FROM users WHERE username = $1"
        row = await self._fetchrow(query, username)
        if not row:
            return None
        return User.from_row(dict(row))

    async def get_by_email(self, email: str) -> User | None:
        """Get user by email."""
        query = f"SELECT {_SELECT_COLS} FROM users WHERE email = $1"
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
        query = f"""
            UPDATE users
            SET hashed_password = $2,
                password_change_required = $3,
                updated_at = now()
            WHERE id = $1
            RETURNING {_SELECT_COLS}
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

    async def list_all(self, search: str | None = None) -> list[User]:
        """List all users, optionally filtered by username/email substring."""
        if search:
            query = f"""
                SELECT {_SELECT_COLS} FROM users
                WHERE username ILIKE $1 OR email ILIKE $1
                ORDER BY created_at DESC
            """
            rows = await self._fetch(query, f"%{search}%")
        else:
            query = f"SELECT {_SELECT_COLS} FROM users ORDER BY created_at DESC"
            rows = await self._fetch(query)
        return [User.from_row(dict(r)) for r in rows]

    async def set_active(self, user_id: UUID, is_active: bool) -> User:
        """Set user is_active flag."""
        query = f"""
            UPDATE users SET is_active = $2, updated_at = now()
            WHERE id = $1
            RETURNING {_SELECT_COLS}
        """
        row = await self._fetchrow(query, user_id, is_active)
        if not row:
            raise RuntimeError("User not found")
        return User.from_row(dict(row))

    async def set_admin(self, user_id: UUID, is_admin: bool) -> User:
        """Set user is_admin flag."""
        query = f"""
            UPDATE users SET is_admin = $2, updated_at = now()
            WHERE id = $1
            RETURNING {_SELECT_COLS}
        """
        row = await self._fetchrow(query, user_id, is_admin)
        if not row:
            raise RuntimeError("User not found")
        return User.from_row(dict(row))

    async def delete(self, user_id: UUID) -> bool:
        """Delete user by ID. Returns True if a row was deleted."""
        query = "DELETE FROM users WHERE id = $1"
        result = await self._execute(query, user_id)
        # asyncpg returns "DELETE N" where N is number of affected rows
        return result == "DELETE 1"

    async def count_admins(self) -> int:
        """Count active admin users."""
        query = "SELECT count(*) FROM users WHERE is_admin = true AND is_active = true"
        row = await self._fetchrow(query)
        return int(row["count"]) if row else 0
