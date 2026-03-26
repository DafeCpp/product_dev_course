"""Domain models for script-service."""
from __future__ import annotations

import json
from datetime import datetime
from enum import StrEnum
from typing import Any
from uuid import UUID


class ScriptType(StrEnum):
    python = "python"
    bash = "bash"
    javascript = "javascript"


class ExecutionStatus(StrEnum):
    pending = "pending"
    running = "running"
    completed = "completed"
    failed = "failed"
    cancelled = "cancelled"
    timeout = "timeout"


class Script:
    __slots__ = (
        "id",
        "name",
        "description",
        "target_service",
        "script_type",
        "script_body",
        "parameters_schema",
        "timeout_sec",
        "is_active",
        "created_by",
        "created_at",
        "updated_at",
    )

    def __init__(
        self,
        id: UUID,
        name: str,
        description: str | None,
        target_service: str,
        script_type: ScriptType,
        script_body: str,
        parameters_schema: dict[str, Any],
        timeout_sec: int,
        is_active: bool,
        created_by: UUID,
        created_at: datetime,
        updated_at: datetime,
    ) -> None:
        self.id = id
        self.name = name
        self.description = description
        self.target_service = target_service
        self.script_type = script_type
        self.script_body = script_body
        self.parameters_schema = parameters_schema
        self.timeout_sec = timeout_sec
        self.is_active = is_active
        self.created_by = created_by
        self.created_at = created_at
        self.updated_at = updated_at

    @classmethod
    def from_row(cls, row: dict[str, Any]) -> "Script":
        schema = row["parameters_schema"]
        if isinstance(schema, str):
            schema = json.loads(schema)
        return cls(
            id=UUID(str(row["id"])),
            name=row["name"],
            description=row.get("description"),
            target_service=row["target_service"],
            script_type=ScriptType(row["script_type"]),
            script_body=row["script_body"],
            parameters_schema=schema,
            timeout_sec=int(row["timeout_sec"]),
            is_active=bool(row["is_active"]),
            created_by=UUID(str(row["created_by"])),
            created_at=row["created_at"],
            updated_at=row["updated_at"],
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "id": str(self.id),
            "name": self.name,
            "description": self.description,
            "target_service": self.target_service,
            "script_type": str(self.script_type),
            "script_body": self.script_body,
            "parameters_schema": self.parameters_schema,
            "timeout_sec": self.timeout_sec,
            "is_active": self.is_active,
            "created_by": str(self.created_by),
            "created_at": self.created_at.isoformat(),
            "updated_at": self.updated_at.isoformat(),
        }


class ScriptExecution:
    __slots__ = (
        "id",
        "script_id",
        "status",
        "parameters",
        "target_instance",
        "requested_by",
        "started_at",
        "finished_at",
        "exit_code",
        "stdout",
        "stderr",
        "error_message",
        "created_at",
        "updated_at",
    )

    def __init__(
        self,
        id: UUID,
        script_id: UUID,
        status: ExecutionStatus,
        parameters: dict[str, Any],
        target_instance: str | None,
        requested_by: UUID,
        started_at: datetime | None,
        finished_at: datetime | None,
        exit_code: int | None,
        stdout: str | None,
        stderr: str | None,
        error_message: str | None,
        created_at: datetime,
        updated_at: datetime,
    ) -> None:
        self.id = id
        self.script_id = script_id
        self.status = status
        self.parameters = parameters
        self.target_instance = target_instance
        self.requested_by = requested_by
        self.started_at = started_at
        self.finished_at = finished_at
        self.exit_code = exit_code
        self.stdout = stdout
        self.stderr = stderr
        self.error_message = error_message
        self.created_at = created_at
        self.updated_at = updated_at

    @classmethod
    def from_row(cls, row: dict[str, Any]) -> "ScriptExecution":
        params = row["parameters"]
        if isinstance(params, str):
            params = json.loads(params)
        return cls(
            id=UUID(str(row["id"])),
            script_id=UUID(str(row["script_id"])),
            status=ExecutionStatus(row["status"]),
            parameters=params,
            target_instance=row.get("target_instance"),
            requested_by=UUID(str(row["requested_by"])),
            started_at=row.get("started_at"),
            finished_at=row.get("finished_at"),
            exit_code=row.get("exit_code"),
            stdout=row.get("stdout"),
            stderr=row.get("stderr"),
            error_message=row.get("error_message"),
            created_at=row["created_at"],
            updated_at=row["updated_at"],
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "id": str(self.id),
            "script_id": str(self.script_id),
            "status": str(self.status),
            "parameters": self.parameters,
            "target_instance": self.target_instance,
            "requested_by": str(self.requested_by),
            "started_at": self.started_at.isoformat() if self.started_at else None,
            "finished_at": self.finished_at.isoformat() if self.finished_at else None,
            "exit_code": self.exit_code,
            "stdout": self.stdout,
            "stderr": self.stderr,
            "error_message": self.error_message,
            "created_at": self.created_at.isoformat(),
            "updated_at": self.updated_at.isoformat(),
        }
