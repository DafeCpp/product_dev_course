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

