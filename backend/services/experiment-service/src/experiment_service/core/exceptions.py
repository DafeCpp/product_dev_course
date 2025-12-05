"""Common exceptions for domain and repository layers."""
from __future__ import annotations


class ExperimentServiceError(Exception):
    """Base error for service layer."""


class RepositoryError(ExperimentServiceError):
    """Raised when repository operations fail."""


class NotFoundError(RepositoryError):
    """Raised when requested entity is missing."""


class ScopeMismatchError(ExperimentServiceError):
    """Raised when entity belongs to a different project."""


class InvalidStatusTransitionError(ExperimentServiceError):
    """Raised when an entity attempts an unsupported status change."""


class IdempotencyConflictError(ExperimentServiceError):
    """Raised when the same idempotency key is reused with a different payload."""


class UnauthorizedError(ExperimentServiceError):
    """Raised when credentials or tokens are invalid."""

