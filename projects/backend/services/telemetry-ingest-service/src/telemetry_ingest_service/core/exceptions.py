"""Domain/service exceptions."""
from __future__ import annotations


class UnauthorizedError(Exception):
    """Raised when sensor token authentication fails."""


class NotFoundError(Exception):
    """Raised when a referenced entity does not exist in expected scope."""


class ScopeMismatchError(Exception):
    """Raised when run/capture session scope does not match."""

