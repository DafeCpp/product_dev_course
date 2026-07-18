"""Domain exceptions for config-service."""
from __future__ import annotations

from backend_common.core.exceptions import (
    ConflictError,
    NotFoundError,
    ServiceError,
    ValidationError,
)


class ConfigNotFoundError(NotFoundError):
    def __init__(self, config_id: str) -> None:
        super().__init__(f"Config not found: {config_id}")
        self.config_id = config_id


class VersionConflictError(ConflictError):
    """Raised when optimistic lock check fails (version mismatch)."""

    def __init__(self, config_id: str, expected: int, actual: int) -> None:
        super().__init__(
            f"Version conflict for config {config_id}: expected {expected}, got {actual}"
        )
        self.config_id = config_id
        self.expected = expected
        self.actual = actual


class SchemaBreakingChangeError(ValidationError):
    """Raised when a schema update contains breaking (non-additive) changes."""

    def __init__(self, violations: list[str]) -> None:
        super().__init__(f"Breaking schema changes: {violations}")
        self.violations = violations

    status_code = 422


class SchemaSanityFailedError(ServiceError):
    """Raised when existing configs fail validation against new schema."""

    def __init__(self, failures: list[dict[str, object]]) -> None:
        super().__init__(f"Schema sanity check failed for {len(failures)} configs")
        self.failures = failures


class IdempotencyConflictError(ConflictError):
    """Raised when an idempotency key is reused with a different request body."""

    def __init__(self, key: str) -> None:
        super().__init__(f"Idempotency key conflict: {key}")
        self.key = key


class ConfigValidationError(ValidationError):
    """Raised when a config value fails JSON Schema validation."""

    def __init__(self, errors: list[str]) -> None:
        super().__init__(f"Config validation failed: {errors}")
        self.errors = errors

    status_code = 422


class SchemaNotFoundError(NotFoundError):
    def __init__(self, config_type: str) -> None:
        super().__init__(f"Schema not found for type: {config_type}")
        self.config_type = config_type
