"""Pydantic models representing key domain entities."""
from __future__ import annotations

from datetime import datetime
from uuid import UUID

from pydantic import BaseModel, Field

from experiment_service.domain.enums import (
    CaptureSessionStatus,
    ConversionProfileStatus,
    ExperimentStatus,
    RunStatus,
)


class Experiment(BaseModel):
    id: UUID
    project_id: UUID
    name: str
    description: str | None = None
    tags: list[str] = Field(default_factory=list)
    status: ExperimentStatus = ExperimentStatus.DRAFT
    owner_id: UUID
    created_at: datetime
    updated_at: datetime
    archived_at: datetime | None = None


class Run(BaseModel):
    id: UUID
    experiment_id: UUID
    params: dict
    git_sha: str | None = None
    env: str | None = None
    status: RunStatus = RunStatus.DRAFT
    started_at: datetime | None = None
    finished_at: datetime | None = None
    duration_seconds: int | None = None
    created_at: datetime
    updated_at: datetime


class CaptureSession(BaseModel):
    id: UUID
    run_id: UUID
    ordinal_number: int
    started_at: datetime | None = None
    stopped_at: datetime | None = None
    status: CaptureSessionStatus = CaptureSessionStatus.DRAFT
    initiated_by: UUID | None = None
    notes: str | None = None


class Sensor(BaseModel):
    id: UUID
    project_id: UUID
    name: str
    type: str
    input_unit: str
    display_unit: str
    token_preview: str | None = None
    status: str
    last_heartbeat: datetime | None = None
    active_profile_id: UUID | None = None


class ConversionProfile(BaseModel):
    id: UUID
    sensor_id: UUID
    version: str
    kind: str
    payload: dict
    status: ConversionProfileStatus = ConversionProfileStatus.DRAFT
    valid_from: datetime | None = None
    valid_to: datetime | None = None


