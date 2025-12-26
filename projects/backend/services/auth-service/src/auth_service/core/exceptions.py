"""Custom exceptions."""
from __future__ import annotations

from aiohttp import web


class AuthError(Exception):
    """Base authentication error."""

    status_code: int = 500
    message: str = "Authentication error"

    def __init__(self, message: str | None = None) -> None:
        super().__init__(message or self.message)
        self.message = message or self.message


class InvalidCredentialsError(AuthError):
    """Invalid username or password."""

    status_code = 401
    message = "Invalid credentials"


class UserNotFoundError(AuthError):
    """User not found."""

    status_code = 404
    message = "User not found"


class UserAlreadyExistsError(AuthError):
    """User already exists."""

    status_code = 409
    message = "User already exists"


class InvalidTokenError(AuthError):
    """Invalid or expired token."""

    status_code = 401
    message = "Invalid or expired token"


class NotFoundError(AuthError):
    """Resource not found."""

    status_code = 404
    message = "Resource not found"


class ForbiddenError(AuthError):
    """Access forbidden."""

    status_code = 403
    message = "Access forbidden"


def handle_auth_error(request: web.Request, error: AuthError) -> web.Response:
    """Handle authentication errors."""
    return web.json_response(
        {"error": error.message},
        status=error.status_code,
    )

