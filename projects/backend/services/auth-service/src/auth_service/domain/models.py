"""Domain models."""
from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime
from typing import Any
from uuid import UUID


@dataclass
class User:
    """User domain model."""

    id: UUID
    username: str
    email: str
    hashed_password: str
    password_change_required: bool
    created_at: datetime
    updated_at: datetime

    @classmethod
    def from_row(cls, row: dict[str, Any]) -> User:
        """Create User from database row."""
        return cls(
            id=row["id"],
            username=row["username"],
            email=row["email"],
            hashed_password=row["hashed_password"],
            password_change_required=row.get("password_change_required", False),
            created_at=row["created_at"],
            updated_at=row["updated_at"],
        )

    def to_dict(self, exclude_password: bool = True) -> dict[str, Any]:
        """Convert to dictionary."""
        data = {
            "id": str(self.id),
            "username": self.username,
            "email": self.email,
            "password_change_required": self.password_change_required,
            "created_at": self.created_at.isoformat(),
            "updated_at": self.updated_at.isoformat(),
        }
        if not exclude_password:
            data["hashed_password"] = self.hashed_password
        return data

