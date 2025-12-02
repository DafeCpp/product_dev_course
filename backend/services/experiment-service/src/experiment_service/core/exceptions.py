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

