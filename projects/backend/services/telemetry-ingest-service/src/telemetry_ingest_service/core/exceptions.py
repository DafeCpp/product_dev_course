"""Domain/service exceptions."""
from __future__ import annotations

from backend_common.core.exceptions import ForbiddenError, NotFoundError, ServiceError, UnauthorizedError

__all__ = ["AuthServiceError", "NotFoundError", "ScopeMismatchError", "UnauthorizedError"]


class ScopeMismatchError(ForbiddenError):
    """Raised when run/capture session scope does not match."""


class AuthServiceError(ServiceError):
    """Raised when the auth-service cannot validate a user token."""

    status_code = 502
