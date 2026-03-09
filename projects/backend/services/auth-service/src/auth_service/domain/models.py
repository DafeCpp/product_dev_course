"""Domain models."""
from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timezone
from enum import StrEnum
from typing import Any
from uuid import UUID


# =============================================================================
# Built-in role UUIDs (must match seed in 001_initial_schema.sql)
# =============================================================================

SUPERADMIN_ROLE_ID = UUID("00000000-0000-0000-0000-000000000001")
ADMIN_ROLE_ID = UUID("00000000-0000-0000-0000-000000000002")
OPERATOR_ROLE_ID = UUID("00000000-0000-0000-0000-000000000003")
AUDITOR_ROLE_ID = UUID("00000000-0000-0000-0000-000000000004")
PROJECT_OWNER_ROLE_ID = UUID("00000000-0000-0000-0000-000000000010")
PROJECT_EDITOR_ROLE_ID = UUID("00000000-0000-0000-0000-000000000011")
PROJECT_VIEWER_ROLE_ID = UUID("00000000-0000-0000-0000-000000000012")


# =============================================================================
# Enums
# =============================================================================

class ScopeType(StrEnum):
    SYSTEM = "system"
    PROJECT = "project"


class AuditAction(StrEnum):
    # Auth
    LOGIN = "auth.login"
    LOGOUT = "auth.logout"
    REGISTER = "auth.register"
    PASSWORD_CHANGE = "auth.password_change"
    PASSWORD_RESET = "auth.password_reset"
    BOOTSTRAP = "auth.bootstrap"
    # Users
    USER_CREATE = "user.create"
    USER_UPDATE = "user.update"
    USER_DEACTIVATE = "user.deactivate"
    USER_DELETE = "user.delete"
    # Roles
    ROLE_GRANT = "role.grant"
    ROLE_REVOKE = "role.revoke"
    ROLE_CREATE = "role.create"
    ROLE_UPDATE = "role.update"
    ROLE_DELETE = "role.delete"
    # Projects
    PROJECT_CREATE = "project.create"
    PROJECT_UPDATE = "project.update"
    PROJECT_DELETE = "project.delete"
    PROJECT_MEMBER_ADD = "project.member.add"
    PROJECT_MEMBER_REMOVE = "project.member.remove"
    PROJECT_MEMBER_CHANGE_ROLE = "project.member.change_role"
    # Scripts
    SCRIPT_EXECUTE = "script.execute"
    SCRIPT_CANCEL = "script.cancel"
    # Configs
    CONFIG_UPDATE = "config.update"
    CONFIG_PUBLISH = "config.publish"


# =============================================================================
# Core models
# =============================================================================

@dataclass
class User:
    """User domain model."""

    id: UUID
    username: str
    email: str
    hashed_password: str
    password_change_required: bool
    is_active: bool
    created_at: datetime
    updated_at: datetime

    @classmethod
    def from_row(cls, row: dict[str, Any]) -> User:
        """Create User from database row."""
        return cls(
            id=row["id"],
            username=row["username"],
            email=row["email"],
            hashed_password=row["hashed_password"],
            password_change_required=row.get("password_change_required", False),
            is_active=row.get("is_active", True),
            created_at=row["created_at"],
            updated_at=row["updated_at"],
        )

    def to_dict(self, exclude_password: bool = True) -> dict[str, Any]:
        """Convert to dictionary."""
        data = {
            "id": str(self.id),
            "username": self.username,
            "email": self.email,
            "password_change_required": self.password_change_required,
            "is_active": self.is_active,
            "created_at": self.created_at.isoformat(),
            "updated_at": self.updated_at.isoformat(),
        }
        if not exclude_password:
            data["hashed_password"] = self.hashed_password
        return data


@dataclass
class InviteToken:
    """Invite token domain model."""

    id: UUID
    token: UUID
    created_by: UUID
    email_hint: str | None
    expires_at: datetime
    used_at: datetime | None
    used_by: UUID | None
    created_at: datetime

    @classmethod
    def from_row(cls, row: dict[str, Any]) -> "InviteToken":
        """Create InviteToken from database row."""
        return cls(
            id=row["id"],
            token=row["token"],
            created_by=row["created_by"],
            email_hint=row.get("email_hint"),
            expires_at=row["expires_at"],
            used_at=row.get("used_at"),
            used_by=row.get("used_by"),
            created_at=row["created_at"],
        )

    @property
    def is_active(self) -> bool:
        """True if token has not been used and has not expired."""
        return self.used_at is None and self.expires_at > datetime.now(timezone.utc)


@dataclass
class Project:
    """Project domain model."""

    id: UUID
    name: str
    description: str | None
    owner_id: UUID
    created_at: datetime
    updated_at: datetime

    @classmethod
    def from_row(cls, row: dict[str, Any]) -> Project:
        """Create Project from database row."""
        return cls(
            id=row["id"],
            name=row["name"],
            description=row.get("description"),
            owner_id=row["owner_id"],
            created_at=row["created_at"],
            updated_at=row["updated_at"],
        )

    def to_dict(self) -> dict[str, Any]:
        """Convert to dictionary."""
        return {
            "id": str(self.id),
            "name": self.name,
            "description": self.description,
            "owner_id": str(self.owner_id),
            "created_at": self.created_at.isoformat(),
            "updated_at": self.updated_at.isoformat(),
        }


# =============================================================================
# RBAC v2 models
# =============================================================================

@dataclass
class Permission:
    """Permission domain model (справочник)."""

    id: str  # e.g. 'experiments.create', 'scripts.execute'
    scope_type: ScopeType
    category: str
    description: str | None
    created_at: datetime

    @classmethod
    def from_row(cls, row: dict[str, Any]) -> Permission:
        return cls(
            id=row["id"],
            scope_type=ScopeType(row["scope_type"]),
            category=row["category"],
            description=row.get("description"),
            created_at=row["created_at"],
        )


@dataclass
class Role:
    """Role domain model (встроенная или кастомная)."""

    id: UUID
    name: str
    scope_type: ScopeType
    project_id: UUID | None
    is_builtin: bool
    description: str | None
    created_by: UUID | None
    created_at: datetime
    updated_at: datetime

    @classmethod
    def from_row(cls, row: dict[str, Any]) -> Role:
        return cls(
            id=row["id"],
            name=row["name"],
            scope_type=ScopeType(row["scope_type"]),
            project_id=row.get("project_id"),
            is_builtin=row.get("is_builtin", False),
            description=row.get("description"),
            created_by=row.get("created_by"),
            created_at=row["created_at"],
            updated_at=row["updated_at"],
        )

    @property
    def is_superadmin(self) -> bool:
        return self.id == SUPERADMIN_ROLE_ID


@dataclass
class RolePermission:
    """Link between a role and a permission."""

    role_id: UUID
    permission_id: str

    @classmethod
    def from_row(cls, row: dict[str, Any]) -> RolePermission:
        return cls(
            role_id=row["role_id"],
            permission_id=row["permission_id"],
        )


@dataclass
class UserSystemRole:
    """Assignment of a system role to a user."""

    user_id: UUID
    role_id: UUID
    granted_by: UUID
    granted_at: datetime
    expires_at: datetime | None

    @classmethod
    def from_row(cls, row: dict[str, Any]) -> UserSystemRole:
        return cls(
            user_id=row["user_id"],
            role_id=row["role_id"],
            granted_by=row["granted_by"],
            granted_at=row["granted_at"],
            expires_at=row.get("expires_at"),
        )

    @property
    def is_expired(self) -> bool:
        if self.expires_at is None:
            return False
        return self.expires_at <= datetime.now(timezone.utc)


@dataclass
class UserProjectRole:
    """Assignment of a project role to a user (replaces ProjectMember)."""

    user_id: UUID
    project_id: UUID
    role_id: UUID
    granted_by: UUID
    granted_at: datetime
    expires_at: datetime | None

    @classmethod
    def from_row(cls, row: dict[str, Any]) -> UserProjectRole:
        return cls(
            user_id=row["user_id"],
            project_id=row["project_id"],
            role_id=row["role_id"],
            granted_by=row["granted_by"],
            granted_at=row["granted_at"],
            expires_at=row.get("expires_at"),
        )

    @property
    def is_expired(self) -> bool:
        if self.expires_at is None:
            return False
        return self.expires_at <= datetime.now(timezone.utc)


@dataclass
class AuditEntry:
    """Audit log entry."""

    id: UUID
    timestamp: datetime
    actor_id: UUID
    action: str
    scope_type: str
    scope_id: UUID | None
    target_type: str | None
    target_id: str | None
    details: dict[str, Any]
    ip_address: str | None
    user_agent: str | None

    @classmethod
    def from_row(cls, row: dict[str, Any]) -> AuditEntry:
        return cls(
            id=row["id"],
            timestamp=row["timestamp"],
            actor_id=row["actor_id"],
            action=row["action"],
            scope_type=row["scope_type"],
            scope_id=row.get("scope_id"),
            target_type=row.get("target_type"),
            target_id=row.get("target_id"),
            details=row.get("details", {}),
            ip_address=str(row["ip_address"]) if row.get("ip_address") else None,
            user_agent=row.get("user_agent"),
        )
