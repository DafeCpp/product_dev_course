"""Data Transfer Objects."""
from __future__ import annotations

import re
from datetime import datetime
from typing import TYPE_CHECKING, Any
from uuid import UUID

from pydantic import BaseModel, EmailStr, Field, field_validator

if TYPE_CHECKING:
    from auth_service.domain.models import (
        AuditEntry,
        InviteToken,
        Permission,
        Project,
        Role,
        User,
        UserProjectRole,
    )

# Minimum password complexity: at least one uppercase, one lowercase, one digit.
_PASSWORD_COMPLEXITY_RE = re.compile(r"^(?=.*[a-z])(?=.*[A-Z])(?=.*\d).+$")

# Set to False in test conftest to skip password complexity checks.
PASSWORD_COMPLEXITY_ENABLED: bool = True


def _check_password_complexity(value: str) -> str:
    """Validate password complexity if enabled."""
    if PASSWORD_COMPLEXITY_ENABLED and not _PASSWORD_COMPLEXITY_RE.match(value):
        raise ValueError(
            "Password must contain at least one uppercase letter, "
            "one lowercase letter, and one digit"
        )
    return value


# =============================================================================
# Auth DTOs
# =============================================================================

class BootstrapAdminRequest(BaseModel):
    """Bootstrap first admin user request."""

    bootstrap_secret: str
    username: str = Field(..., min_length=3, max_length=50)
    email: EmailStr
    password: str = Field(..., min_length=8, max_length=100)

    @field_validator("password")
    @classmethod
    def _password_complexity(cls, value: str) -> str:
        return _check_password_complexity(value)


class UserRegisterRequest(BaseModel):
    """User registration request."""

    username: str = Field(..., min_length=3, max_length=50)
    email: EmailStr
    password: str = Field(..., min_length=8, max_length=100)
    invite_token: UUID | None = None

    @field_validator("password")
    @classmethod
    def _password_complexity(cls, value: str) -> str:
        return _check_password_complexity(value)


class UserLoginRequest(BaseModel):
    """User login request."""

    username: str
    password: str


class TokenRefreshRequest(BaseModel):
    """Token refresh request."""

    refresh_token: str


class LogoutRequest(BaseModel):
    """Logout request."""

    refresh_token: str


class AuthTokensResponse(BaseModel):
    """Authentication tokens response."""

    access_token: str
    refresh_token: str


class UserResponse(BaseModel):
    """User response (без is_admin — заменён системными ролями)."""

    id: str
    username: str
    email: str
    password_change_required: bool = False
    is_active: bool = True
    system_roles: list[str] = Field(default_factory=list)

    @classmethod
    def from_user(cls, user: "User", system_roles: list[str] | None = None) -> "UserResponse":
        """Create UserResponse from a User domain model."""
        return cls(
            id=str(user.id),
            username=user.username,
            email=user.email,
            password_change_required=user.password_change_required,
            is_active=user.is_active,
            system_roles=system_roles or [],
        )


class PasswordResetRequestDto(BaseModel):
    """Password reset request (by email)."""

    email: EmailStr


class PasswordResetConfirmRequest(BaseModel):
    """Password reset confirmation."""

    reset_token: str
    new_password: str = Field(..., min_length=8, max_length=100)

    @field_validator("new_password")
    @classmethod
    def _password_complexity(cls, value: str) -> str:
        return _check_password_complexity(value)


class AdminUserResetRequest(BaseModel):
    """Admin reset of another user's password."""

    new_password: str | None = None


class AdminUserUpdateRequest(BaseModel):
    """Admin update of user fields."""

    is_active: bool | None = None


class PasswordChangeRequest(BaseModel):
    """Password change request."""

    old_password: str
    new_password: str = Field(..., min_length=8, max_length=100)

    @field_validator("new_password")
    @classmethod
    def _password_complexity(cls, value: str) -> str:
        return _check_password_complexity(value)


# =============================================================================
# Invite DTOs
# =============================================================================

class InviteCreateRequest(BaseModel):
    """Invite creation request."""

    email_hint: str | None = None
    expires_in_hours: int = Field(default=72, ge=1, le=8760)


class InviteResponse(BaseModel):
    """Invite token response."""

    id: UUID
    token: UUID
    created_by: UUID
    email_hint: str | None
    expires_at: datetime
    used_at: datetime | None
    used_by: UUID | None
    created_at: datetime
    is_active: bool

    @classmethod
    def from_model(cls, inv: "InviteToken") -> "InviteResponse":
        """Create InviteResponse from an InviteToken domain model."""
        return cls(
            id=inv.id,
            token=inv.token,
            created_by=inv.created_by,
            email_hint=inv.email_hint,
            expires_at=inv.expires_at,
            used_at=inv.used_at,
            used_by=inv.used_by,
            created_at=inv.created_at,
            is_active=inv.is_active,
        )


# =============================================================================
# Project DTOs
# =============================================================================

class ProjectCreateRequest(BaseModel):
    """Project creation request."""

    name: str = Field(..., min_length=1, max_length=200)
    description: str | None = Field(None, max_length=1000)


class ProjectUpdateRequest(BaseModel):
    """Project update request."""

    name: str | None = Field(None, min_length=1, max_length=200)
    description: str | None = Field(None, max_length=1000)


class ProjectResponse(BaseModel):
    """Project response."""

    id: str
    name: str
    description: str | None
    owner_id: str
    created_at: str
    updated_at: str

    @classmethod
    def from_project(cls, project: "Project") -> "ProjectResponse":
        """Create ProjectResponse from a Project domain model."""
        return cls(
            id=str(project.id),
            name=project.name,
            description=project.description,
            owner_id=str(project.owner_id),
            created_at=project.created_at.isoformat(),
            updated_at=project.updated_at.isoformat(),
        )


class ProjectMemberResponse(BaseModel):
    """Project member response (через user_project_roles)."""

    project_id: str
    user_id: str
    roles: list[str]
    username: str | None = None
    granted_at: str | None = None


class ProjectMemberAddRequest(BaseModel):
    """Request to add a member to a project (grant a role)."""

    user_id: str
    role_id: str
    expires_at: str | None = None  # ISO 8601 datetime


class ProjectMemberUpdateRequest(BaseModel):
    """Request to update a member's role."""

    role_id: str


class GrantProjectRoleRequest(BaseModel):
    """Request to grant a project role to a user."""

    role_id: UUID
    expires_at: datetime | None = None


# =============================================================================
# RBAC v2: Permission DTOs
# =============================================================================

class PermissionResponse(BaseModel):
    """Single permission from the catalog."""

    id: str
    scope_type: str
    category: str
    description: str | None = None

    @classmethod
    def from_model(cls, perm: "Permission") -> "PermissionResponse":
        return cls(
            id=perm.id,
            scope_type=perm.scope_type.value,
            category=perm.category,
            description=perm.description,
        )


class EffectivePermissionsResponse(BaseModel):
    """Effective permissions for a user in a given scope."""

    user_id: str
    is_superadmin: bool = False
    system_permissions: list[str] = Field(default_factory=list)
    project_permissions: list[str] = Field(default_factory=list)


# =============================================================================
# RBAC v2: Role DTOs
# =============================================================================

class CreateRoleRequest(BaseModel):
    """Create a custom role."""

    name: str = Field(..., min_length=1, max_length=100)
    description: str | None = Field(None, max_length=500)
    permissions: list[str] = Field(..., min_length=1)


class UpdateRoleRequest(BaseModel):
    """Update a custom role."""

    name: str | None = Field(None, min_length=1, max_length=100)
    description: str | None = Field(None, max_length=500)
    permissions: list[str] | None = None


class RoleResponse(BaseModel):
    """Role response."""

    id: str
    name: str
    scope_type: str
    project_id: str | None = None
    is_builtin: bool = False
    description: str | None = None
    permissions: list[str] = Field(default_factory=list)
    created_at: str
    updated_at: str

    @classmethod
    def from_model(cls, role: "Role", permissions: list[str] | None = None) -> "RoleResponse":
        return cls(
            id=str(role.id),
            name=role.name,
            scope_type=role.scope_type.value,
            project_id=str(role.project_id) if role.project_id else None,
            is_builtin=role.is_builtin,
            description=role.description,
            permissions=permissions or [],
            created_at=role.created_at.isoformat(),
            updated_at=role.updated_at.isoformat(),
        )


class GrantRoleRequest(BaseModel):
    """Assign a role to a user."""

    role_id: UUID
    expires_at: datetime | None = None


class RevokeRoleRequest(BaseModel):
    """Revoke a role from a user (used for DELETE body if needed)."""

    role_id: UUID


# =============================================================================
# Audit DTOs
# =============================================================================

class AuditLogEntry(BaseModel):
    """Single audit log entry response."""

    id: str
    timestamp: str
    actor_id: str
    action: str
    scope_type: str
    scope_id: str | None = None
    target_type: str | None = None
    target_id: str | None = None
    details: dict[str, Any] = Field(default_factory=dict)
    ip_address: str | None = None
    user_agent: str | None = None

    @classmethod
    def from_model(cls, entry: "AuditEntry") -> "AuditLogEntry":
        return cls(
            id=str(entry.id),
            timestamp=entry.timestamp.isoformat(),
            actor_id=str(entry.actor_id),
            action=entry.action,
            scope_type=entry.scope_type,
            scope_id=str(entry.scope_id) if entry.scope_id else None,
            target_type=entry.target_type,
            target_id=entry.target_id,
            details=entry.details,
            ip_address=entry.ip_address,
            user_agent=entry.user_agent,
        )


class AuditLogQuery(BaseModel):
    """Query parameters for audit log search."""

    actor_id: UUID | None = None
    action: str | None = None
    scope_type: str | None = None
    scope_id: UUID | None = None
    target_type: str | None = None
    target_id: str | None = None
    from_date: datetime | None = Field(None, alias="from")
    to_date: datetime | None = Field(None, alias="to")
    limit: int = Field(default=50, ge=1, le=500)
    offset: int = Field(default=0, ge=0)

    model_config = {"populate_by_name": True}
