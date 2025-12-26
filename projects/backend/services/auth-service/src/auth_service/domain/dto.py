"""Data Transfer Objects."""
from __future__ import annotations

from pydantic import BaseModel, EmailStr, Field


class UserRegisterRequest(BaseModel):
    """User registration request."""

    username: str = Field(..., min_length=3, max_length=50)
    email: EmailStr
    password: str = Field(..., min_length=8, max_length=100)


class UserLoginRequest(BaseModel):
    """User login request."""

    username: str
    password: str


class TokenRefreshRequest(BaseModel):
    """Token refresh request."""

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


class PasswordChangeRequest(BaseModel):
    """Password change request."""

    old_password: str
    new_password: str = Field(..., min_length=8, max_length=100)


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

