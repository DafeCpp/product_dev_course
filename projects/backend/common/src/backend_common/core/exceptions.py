"""Base exception classes for backend services."""
from __future__ import annotations


class ServiceError(Exception):
    """Base error for service layer."""


class RepositoryError(ServiceError):
    """Raised when repository operations fail."""


class NotFoundError(RepositoryError):
    """Raised when requested entity is missing."""


class UnauthorizedError(ServiceError):
    """Raised when credentials or tokens are invalid."""


class ForbiddenError(ServiceError):
    """Raised when access is forbidden."""


class ConflictError(ServiceError):
    """Raised when there is a conflict (e.g., duplicate resource)."""

