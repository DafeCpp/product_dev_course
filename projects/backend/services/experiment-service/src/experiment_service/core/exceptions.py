"""Common exceptions for domain and repository layers."""
from __future__ import annotations

from backend_common.core.exceptions import ServiceError


class ExperimentServiceError(ServiceError):
    """Base error for experiment-service layer.

    Inherits from ``ServiceError`` so the common error-handling middleware
    can map it to an HTTP response automatically via ``status_code``.
    """


class RepositoryError(ExperimentServiceError):
    """Raised when repository operations fail."""


class NotFoundError(RepositoryError):
    """Raised when requested entity is missing."""

    status_code: int = 404


class DuplicateResourceError(RepositoryError):
    """Raised when an insert violates a uniqueness constraint.

    Converts a leaked ``asyncpg.UniqueViolationError`` (which would otherwise
    surface as a 500) into a clean 409 — e.g. two concurrent creates racing on
    ``experiments_project_name_uindex`` / ``sensors_project_name_uindex``.
    """

    status_code: int = 409


class ScopeMismatchError(ExperimentServiceError):
    """Raised when entity belongs to a different project."""

    status_code: int = 403


class InvalidStatusTransitionError(ExperimentServiceError):
    """Raised when an entity attempts an unsupported status change."""

    status_code: int = 409


class IdempotencyConflictError(ExperimentServiceError):
    """Raised when the same idempotency key is reused with a different payload."""

    status_code: int = 409


class UnauthorizedError(ExperimentServiceError):
    """Raised when credentials or tokens are invalid."""

    status_code: int = 401

