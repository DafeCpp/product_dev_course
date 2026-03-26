"""Domain/service exceptions."""
from __future__ import annotations

from backend_common.core.exceptions import ServiceError


class UnauthorizedError(ServiceError):
    """Raised when sensor token authentication fails."""

    status_code: int = 401


class NotFoundError(ServiceError):
    """Raised when a referenced entity does not exist in expected scope."""

    status_code: int = 404


class ScopeMismatchError(ServiceError):
    """Raised when run/capture session scope does not match."""

    status_code: int = 403

