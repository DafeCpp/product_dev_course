"""Pydantic models for script runner message protocol."""
from __future__ import annotations

from typing import Any
from uuid import UUID

from pydantic import BaseModel


class ExecuteCommand(BaseModel):
    """Message to trigger script execution."""

    execution_id: UUID
    script_id: UUID
    script_body: str
    script_type: str  # 'python', 'bash', 'javascript'
    parameters: dict[str, Any] = {}
    timeout_sec: int = 30
    target_instance: str | None = None
    requested_by: UUID


class CancelCommand(BaseModel):
    """Message to cancel a running execution."""

    execution_id: UUID
    script_id: UUID
    cancelled_by: UUID


class StatusReport(BaseModel):
    """Message reporting execution status change."""

    execution_id: UUID
    status: str  # 'running', 'completed', 'failed', 'cancelled', 'timeout'
    exit_code: int | None = None
    stdout: str | None = None
    stderr: str | None = None
    error_message: str | None = None
