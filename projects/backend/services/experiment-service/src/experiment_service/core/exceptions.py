"""Common exceptions for domain and repository layers."""
from __future__ import annotations

from backend_common.core.exceptions import (
    ConflictError,
    ForbiddenError,
    InvalidStatusTransitionError,
    NotFoundError,
    ServiceError,
    UnauthorizedError,
    ValidationError,
)

__all__ = [
    "ActiveStateConflictError",
    "DuplicateResourceError",
    "EmptyUpdateError",
    "ExperimentServiceError",
    "IdempotencyConflictError",
    "InvalidInitialStatusError",
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

class ActiveStateConflictError(ConflictError):
    """Raised when an operation is refused because a related entity is active (409).

    E.g. deleting an experiment that still has a running run, or finishing a
    run while one of its capture sessions is still recording.  The message
    names the blocking state so the client can show it to the user.
    """


class InvalidInitialStatusError(ValidationError):
    """Raised when a create request asks for a status a new entity cannot start in (400).

    Unlike ``InvalidStatusTransitionError`` (409), nothing exists yet whose
    state could conflict: the request body itself is invalid.
    """


class EmptyUpdateError(ValidationError):
    """Raised when a partial update carries no non-null fields (400)."""

    def __init__(self) -> None:
        super().__init__("No fields provided for update")


class ScopeMismatchError(ForbiddenError):
    """Raised when entity belongs to a different project."""

class IdempotencyConflictError(ConflictError):
    """Raised when the same idempotency key is reused with a different payload."""

