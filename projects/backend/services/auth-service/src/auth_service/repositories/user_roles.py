"""User role assignment repository."""
from __future__ import annotations

from datetime import datetime
from uuid import UUID

from auth_service.domain.models import (
    SUPERADMIN_ROLE_ID,
    UserProjectRole,
    UserSystemRole,
)
from auth_service.repositories.base import BaseRepository


class UserRoleRepository(BaseRepository):
    """Repository for user ↔ role assignments and effective permission queries."""

    # ── System roles ────────────────────────────────────────────────────

    async def grant_system_role(
        self,
        user_id: UUID,
        role_id: UUID,
        granted_by: UUID,
        expires_at: datetime | None = None,
    ) -> UserSystemRole:
        """Grant a system role to a user (upsert)."""
        row = await self._fetchrow(
            "INSERT INTO user_system_roles (user_id, role_id, granted_by, expires_at) "
            "VALUES ($1, $2, $3, $4) "
            "ON CONFLICT (user_id, role_id) DO UPDATE "
            "SET granted_by = EXCLUDED.granted_by, granted_at = now(), expires_at = EXCLUDED.expires_at "
            "RETURNING user_id, role_id, granted_by, granted_at, expires_at",
            user_id, role_id, granted_by, expires_at,
        )
        assert row is not None
        return UserSystemRole.from_row(dict(row))

    async def revoke_system_role(self, user_id: UUID, role_id: UUID) -> bool:
        """Revoke a system role. Returns True if a row was deleted."""
        result = await self._execute(
            "DELETE FROM user_system_roles WHERE user_id = $1 AND role_id = $2",
            user_id, role_id,
        )
        return result == "DELETE 1"

    async def list_system_roles(self, user_id: UUID) -> list[UserSystemRole]:
        """List all system roles assigned to a user."""
        rows = await self._fetch(
            "SELECT usr.user_id, usr.role_id, usr.granted_by, usr.granted_at, usr.expires_at "
            "FROM user_system_roles usr "
            "WHERE usr.user_id = $1",
            user_id,
        )
        return [UserSystemRole.from_row(dict(r)) for r in rows]

    async def list_system_role_names(self, user_id: UUID) -> list[str]:
        """List system role names for a user (for UserResponse.system_roles)."""
        rows = await self._fetch(
            "SELECT r.name FROM user_system_roles usr "
            "JOIN roles r ON r.id = usr.role_id "
            "WHERE usr.user_id = $1 "
            "AND (usr.expires_at IS NULL OR usr.expires_at > now())",
            user_id,
        )
        return [row["name"] for row in rows]

    # ── Project roles ───────────────────────────────────────────────────

    async def grant_project_role(
        self,
        user_id: UUID,
        project_id: UUID,
        role_id: UUID,
        granted_by: UUID,
        expires_at: datetime | None = None,
    ) -> UserProjectRole:
        """Grant a project role to a user (upsert)."""
        row = await self._fetchrow(
            "INSERT INTO user_project_roles (user_id, project_id, role_id, granted_by, expires_at) "
            "VALUES ($1, $2, $3, $4, $5) "
            "ON CONFLICT (user_id, project_id, role_id) DO UPDATE "
            "SET granted_by = EXCLUDED.granted_by, granted_at = now(), expires_at = EXCLUDED.expires_at "
            "RETURNING user_id, project_id, role_id, granted_by, granted_at, expires_at",
            user_id, project_id, role_id, granted_by, expires_at,
        )
        assert row is not None
        return UserProjectRole.from_row(dict(row))

    async def revoke_project_role(
        self, user_id: UUID, project_id: UUID, role_id: UUID,
    ) -> bool:
        """Revoke a project role. Returns True if a row was deleted."""
        result = await self._execute(
            "DELETE FROM user_project_roles "
            "WHERE user_id = $1 AND project_id = $2 AND role_id = $3",
            user_id, project_id, role_id,
        )
        return result == "DELETE 1"

    async def list_project_roles(
        self, user_id: UUID, project_id: UUID,
    ) -> list[UserProjectRole]:
        """List all project roles for a user in a specific project."""
        rows = await self._fetch(
            "SELECT user_id, project_id, role_id, granted_by, granted_at, expires_at "
            "FROM user_project_roles "
            "WHERE user_id = $1 AND project_id = $2",
            user_id, project_id,
        )
        return [UserProjectRole.from_row(dict(r)) for r in rows]

    # ── Effective permissions ───────────────────────────────────────────

    async def get_effective_permissions(
        self, user_id: UUID, project_id: UUID | None = None,
    ) -> list[str]:
        """Get the effective permission IDs for a user.

        - If the user has the superadmin role, returns an empty list
          (caller should check is_superadmin separately and grant all).
        - Collects permissions from all non-expired system roles.
        - If project_id is given, also collects permissions from
          non-expired project roles in that project.
        """
        # System role permissions
        rows = await self._fetch(
            "SELECT DISTINCT rp.permission_id "
            "FROM user_system_roles usr "
            "JOIN role_permissions rp ON rp.role_id = usr.role_id "
            "WHERE usr.user_id = $1 "
            "AND (usr.expires_at IS NULL OR usr.expires_at > now())",
            user_id,
        )
        perms = {row["permission_id"] for row in rows}

        # Project role permissions
        if project_id is not None:
            rows = await self._fetch(
                "SELECT DISTINCT rp.permission_id "
                "FROM user_project_roles upr "
                "JOIN role_permissions rp ON rp.role_id = upr.role_id "
                "WHERE upr.user_id = $1 AND upr.project_id = $2 "
                "AND (upr.expires_at IS NULL OR upr.expires_at > now())",
                user_id, project_id,
            )
            perms.update(row["permission_id"] for row in rows)

        return sorted(perms)

    async def count_project_role(self, project_id: UUID, role_id: UUID) -> int:
        """Count users with an active specific role in a project."""
        row = await self._fetchrow(
            "SELECT count(*) FROM user_project_roles "
            "WHERE project_id = $1 AND role_id = $2 "
            "AND (expires_at IS NULL OR expires_at > now())",
            project_id, role_id,
        )
        return int(row["count"]) if row else 0

    # ── Superadmin check ────────────────────────────────────────────────

    async def is_superadmin(self, user_id: UUID) -> bool:
        """Check if user has the superadmin system role (non-expired)."""
        row = await self._fetchrow(
            "SELECT EXISTS("
            "  SELECT 1 FROM user_system_roles "
            "  WHERE user_id = $1 AND role_id = $2 "
            "  AND (expires_at IS NULL OR expires_at > now())"
            ") AS is_sa",
            user_id, SUPERADMIN_ROLE_ID,
        )
        return bool(row["is_sa"]) if row else False

    async def count_superadmins(self) -> int:
        """Count users with active superadmin role."""
        row = await self._fetchrow(
            "SELECT count(*) FROM user_system_roles "
            "WHERE role_id = $1 "
            "AND (expires_at IS NULL OR expires_at > now())",
            SUPERADMIN_ROLE_ID,
        )
        return int(row["count"]) if row else 0

    # ── Project members ─────────────────────────────────────────────────

    async def list_project_members(self, project_id: UUID) -> list[dict]:
        """List all members of a project with their roles and usernames.

        Returns a list of dicts with keys:
        project_id, user_id, username, roles (list[str]), granted_at (earliest).
        """
        rows = await self._fetch(
            "SELECT upr.user_id, u.username, r.name AS role_name, upr.granted_at "
            "FROM user_project_roles upr "
            "JOIN users u ON u.id = upr.user_id "
            "JOIN roles r ON r.id = upr.role_id "
            "WHERE upr.project_id = $1 "
            "AND (upr.expires_at IS NULL OR upr.expires_at > now()) "
            "ORDER BY upr.user_id, r.name",
            project_id,
        )

        # Group by user
        members: dict[UUID, dict] = {}
        for row in rows:
            uid = row["user_id"]
            if uid not in members:
                members[uid] = {
                    "project_id": project_id,
                    "user_id": uid,
                    "username": row["username"],
                    "roles": [],
                    "granted_at": row["granted_at"],
                }
            members[uid]["roles"].append(row["role_name"])
            # Keep the earliest granted_at
            if row["granted_at"] < members[uid]["granted_at"]:
                members[uid]["granted_at"] = row["granted_at"]

        return list(members.values())

    async def has_project_role(self, user_id: UUID, project_id: UUID) -> bool:
        """Check if user has any active role in the project."""
        row = await self._fetchrow(
            "SELECT EXISTS("
            "  SELECT 1 FROM user_project_roles "
            "  WHERE user_id = $1 AND project_id = $2 "
            "  AND (expires_at IS NULL OR expires_at > now())"
            ") AS has_role",
            user_id, project_id,
        )
        return bool(row["has_role"]) if row else False
