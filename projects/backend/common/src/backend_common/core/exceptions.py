"""Base exception classes for backend services."""
from __future__ import annotations


class ServiceError(Exception):
    """Base error for service layer.

    Subclasses may set ``status_code`` to allow the error-handling middleware
    to map them to HTTP responses automatically.
    """

    status_code: int = 500


class RepositoryError(ServiceError):
    """Raised when repository operations fail."""


class NotFoundError(RepositoryError):
    """Raised when requested entity is missing."""

    status_code: int = 404


class UnauthorizedError(ServiceError):
    """Raised when credentials or tokens are invalid."""

    status_code: int = 401


class ForbiddenError(ServiceError):
    """Raised when access is forbidden."""

    status_code: int = 403


class ConflictError(ServiceError):
    """Raised when there is a conflict (e.g., duplicate resource)."""

    status_code: int = 409


class ValidationError(ServiceError):
    """Raised when request validation fails."""

    status_code: int = 400


class InvalidStatusTransitionError(ServiceError):
    """Raised when an entity attempts an unsupported status change."""

    status_code: int = 409

