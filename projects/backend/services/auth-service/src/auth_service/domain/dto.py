"""Data Transfer Objects."""
from __future__ import annotations

import re
from datetime import datetime
from typing import TYPE_CHECKING
from uuid import UUID

from pydantic import BaseModel, EmailStr, Field, field_validator

if TYPE_CHECKING:
    from auth_service.domain.models import InviteToken, Project, ProjectMember, User

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
    """User response."""

    id: str
    username: str
    email: str
    password_change_required: bool = False
    is_admin: bool = False
    is_active: bool = True

    @classmethod
    def from_user(cls, user: "User") -> "UserResponse":
        """Create UserResponse from a User domain model."""
        return cls(
            id=str(user.id),
            username=user.username,
            email=user.email,
            password_change_required=user.password_change_required,
            is_admin=user.is_admin,
            is_active=user.is_active,
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
    """Admin update of user fields (is_active, is_admin)."""

    is_active: bool | None = None
    is_admin: bool | None = None


class PasswordChangeRequest(BaseModel):
    """Password change request."""

    old_password: str
    new_password: str = Field(..., min_length=8, max_length=100)

    @field_validator("new_password")
    @classmethod
    def _password_complexity(cls, value: str) -> str:
        return _check_password_complexity(value)


# Invite DTOs

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


# Project DTOs
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


class ProjectMemberAddRequest(BaseModel):
    """Add member to project request."""

    user_id: str
    role: str = Field(..., pattern="^(owner|editor|viewer)$")


class ProjectMemberUpdateRequest(BaseModel):
    """Update project member role request."""

    role: str = Field(..., pattern="^(owner|editor|viewer)$")


class ProjectMemberResponse(BaseModel):
    """Project member response."""

    project_id: str
    user_id: str
    role: str
    created_at: str
    username: str | None = None  # Optional, populated when joining with users table

