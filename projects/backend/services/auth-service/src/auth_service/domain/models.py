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


@dataclass
class Project:
    """Project domain model."""

    id: UUID
    name: str
    description: str | None
    owner_id: UUID
    created_at: datetime
    updated_at: datetime

    @classmethod
    def from_row(cls, row: dict[str, Any]) -> Project:
        """Create Project from database row."""
        return cls(
            id=row["id"],
            name=row["name"],
            description=row.get("description"),
            owner_id=row["owner_id"],
            created_at=row["created_at"],
            updated_at=row["updated_at"],
        )

    def to_dict(self) -> dict[str, Any]:
        """Convert to dictionary."""
        return {
            "id": str(self.id),
            "name": self.name,
            "description": self.description,
            "owner_id": str(self.owner_id),
            "created_at": self.created_at.isoformat(),
            "updated_at": self.updated_at.isoformat(),
        }


@dataclass
class ProjectMember:
    """Project member domain model."""

    project_id: UUID
    user_id: UUID
    role: str  # 'owner', 'editor', 'viewer'
    created_at: datetime

    @classmethod
    def from_row(cls, row: dict[str, Any]) -> ProjectMember:
        """Create ProjectMember from database row."""
        return cls(
            project_id=row["project_id"],
            user_id=row["user_id"],
            role=row["role"],
            created_at=row["created_at"],
        )

    def to_dict(self) -> dict[str, Any]:
        """Convert to dictionary."""
        return {
            "project_id": str(self.project_id),
            "user_id": str(self.user_id),
            "role": self.role,
            "created_at": self.created_at.isoformat(),
        }

