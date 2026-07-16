"""Common exceptions for domain and repository layers."""
from __future__ import annotations

from backend_common.core.exceptions import (
    ConflictError,
    ForbiddenError,
    InvalidStatusTransitionError,
    NotFoundError,
    ServiceError,
    UnauthorizedError,
)

__all__ = [
    "DuplicateResourceError",
    "ExperimentServiceError",
    "IdempotencyConflictError",
    "InvalidStatusTransitionError",
    "NotFoundError",
    "ScopeMismatchError",
    "UnauthorizedError",
]


class ExperimentServiceError(ServiceError):
    """Base error for experiment-service layer.

    Inherits from ``ServiceError`` so the common error-handling middleware
    can map it to an HTTP response automatically via ``status_code``.
    """


class DuplicateResourceError(ConflictError):
    """Raised when an insert violates a uniqueness constraint.

    Converts a leaked ``asyncpg.UniqueViolationError`` (which would otherwise
    surface as a 500) into a clean 409 — e.g. two concurrent creates racing on
    ``experiments_project_name_uindex`` / ``sensors_project_name_uindex``.
    """

class ScopeMismatchError(ForbiddenError):
    """Raised when entity belongs to a different project."""

class IdempotencyConflictError(ConflictError):
    """Raised when the same idempotency key is reused with a different payload."""

