"""Domain/service exceptions."""
from __future__ import annotations

from backend_common.core.exceptions import ForbiddenError, NotFoundError, UnauthorizedError

__all__ = ["NotFoundError", "ScopeMismatchError", "UnauthorizedError"]


class ScopeMismatchError(ForbiddenError):
    """Raised when run/capture session scope does not match."""

