"""Custom exceptions."""
from __future__ import annotations

from aiohttp import web

from backend_common.core.exceptions import (
    ConflictError,
    ForbiddenError,
    NotFoundError,
    ServiceError,
    UnauthorizedError,
)

__all__ = [
    "AuthError",
    "ConflictError",
    "ForbiddenError",
    "InvalidCredentialsError",
    "InvalidTokenError",
    "NotFoundError",
    "UnauthorizedError",
    "UserAlreadyExistsError",
    "UserNotFoundError",
    "handle_auth_error",
]


class AuthError(ServiceError):
    """Base authentication error.

    Inherits from ``ServiceError`` so the common error-handling middleware
    can map it to an HTTP response automatically via ``status_code``.
    """

    status_code: int = 500
    message: str = "Authentication error"

    def __init__(self, message: str | None = None) -> None:
        super().__init__(message or self.message)
        self.message = message or self.message


class InvalidCredentialsError(UnauthorizedError, AuthError):
    """Invalid username or password."""

    status_code = 401
    message = "Invalid credentials"


class UserNotFoundError(NotFoundError, AuthError):
    """User not found."""

    status_code = 404
    message = "User not found"


class UserAlreadyExistsError(ConflictError, AuthError):
    """User already exists."""

    status_code = 409
    message = "User already exists"


class InvalidTokenError(UnauthorizedError, AuthError):
    """Invalid or expired token."""

    status_code = 401
    message = "Invalid or expired token"


def handle_auth_error(request: web.Request, error: ServiceError) -> web.Response:
    """Handle authentication errors."""
    return web.json_response(
        {"error": str(error) or type(error).__name__},
        status=error.status_code,
    )
