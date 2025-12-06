"""Pydantic DTOs for repository/service layers."""
from __future__ import annotations

from datetime import datetime
from typing import Any
from uuid import UUID

# pyright: reportMissingImports=false

from pydantic import BaseModel, ConfigDict, Field, field_validator

from experiment_service.domain.enums import (
    CaptureSessionStatus,
    ConversionProfileStatus,
    ExperimentStatus,
    RunStatus,
    SensorStatus,
    TelemetryConversionStatus,
)


class ExperimentCreateDTO(BaseModel):
    model_config = ConfigDict(extra="forbid")

    project_id: UUID
    owner_id: UUID
    name: str
    description: str | None = None
    experiment_type: str | None = None
    tags: list[str] = Field(default_factory=list)
    metadata: dict[str, Any] = Field(default_factory=dict)
    status: ExperimentStatus = ExperimentStatus.DRAFT


class ExperimentUpdateDTO(BaseModel):
    model_config = ConfigDict(extra="forbid")

    name: str | None = None
    description: str | None = None
    experiment_type: str | None = None
    tags: list[str] | None = None
    metadata: dict[str, Any] | None = None
    status: ExperimentStatus | None = None
    archived_at: datetime | None = None


class RunCreateDTO(BaseModel):
    model_config = ConfigDict(extra="forbid")

    experiment_id: UUID
    project_id: UUID
    created_by: UUID
    name: str | None = None
    params: dict[str, Any] = Field(default_factory=dict)
    git_sha: str | None = None
    env: str | None = None
    notes: str | None = None
    metadata: dict[str, Any] = Field(default_factory=dict)
    status: RunStatus = RunStatus.DRAFT
    started_at: datetime | None = None
    finished_at: datetime | None = None
    duration_seconds: int | None = None


class RunUpdateDTO(BaseModel):
    model_config = ConfigDict(extra="forbid")

    name: str | None = None
    params: dict[str, Any] | None = None
    git_sha: str | None = None
    env: str | None = None
    notes: str | None = None
    metadata: dict[str, Any] | None = None
    status: RunStatus | None = None
    started_at: datetime | None = None
    finished_at: datetime | None = None
    duration_seconds: int | None = None


class CaptureSessionCreateDTO(BaseModel):
    model_config = ConfigDict(extra="forbid")

    run_id: UUID
    project_id: UUID
    ordinal_number: int
    status: CaptureSessionStatus = CaptureSessionStatus.DRAFT
    initiated_by: UUID | None = None
    notes: str | None = None
    started_at: datetime | None = None
    stopped_at: datetime | None = None
    archived: bool = False


class CaptureSessionUpdateDTO(BaseModel):
    model_config = ConfigDict(extra="forbid")

    status: CaptureSessionStatus | None = None
    notes: str | None = None
    started_at: datetime | None = None
    stopped_at: datetime | None = None
    archived: bool | None = None


class SensorCreateDTO(BaseModel):
    model_config = ConfigDict(extra="forbid")

    project_id: UUID
    name: str
    type: str
    input_unit: str
    display_unit: str
    status: SensorStatus = SensorStatus.REGISTERING
    calibration_notes: str | None = None


class SensorUpdateDTO(BaseModel):
    model_config = ConfigDict(extra="forbid")

    name: str | None = None
    type: str | None = None
    input_unit: str | None = None
    display_unit: str | None = None
    status: SensorStatus | None = None
    calibration_notes: str | None = None
    last_heartbeat: datetime | None = None
    active_profile_id: UUID | None = None


class ConversionProfileInputDTO(BaseModel):
    model_config = ConfigDict(extra="forbid")

    version: str
    kind: str
    payload: dict[str, Any] = Field(default_factory=dict)
    status: ConversionProfileStatus = ConversionProfileStatus.DRAFT
    valid_from: datetime | None = None
    valid_to: datetime | None = None


class ConversionProfileCreateDTO(ConversionProfileInputDTO):
    sensor_id: UUID
    project_id: UUID
    created_by: UUID


class ConversionProfilePublishDTO(BaseModel):
    model_config = ConfigDict(extra="forbid")

    sensor_id: UUID
    project_id: UUID
    profile_id: UUID
    published_by: UUID


class TelemetryReadingDTO(BaseModel):
    model_config = ConfigDict(extra="forbid")

    timestamp: datetime
    raw_value: float
    physical_value: float | None = None
    meta: dict[str, Any] = Field(default_factory=dict)


class TelemetryIngestDTO(BaseModel):
    model_config = ConfigDict(extra="forbid")

    sensor_id: UUID
    run_id: UUID | None = None
    capture_session_id: UUID | None = None
    readings: list[TelemetryReadingDTO] = Field(default_factory=list)

    @field_validator("readings")
    @classmethod
    def _validate_readings(cls, value: list[TelemetryReadingDTO]) -> list[TelemetryReadingDTO]:
        if not value:
            raise ValueError("readings must not be empty")
        return value


class TelemetryRecordCreateDTO(BaseModel):
    model_config = ConfigDict(extra="forbid")

    project_id: UUID
    sensor_id: UUID
    run_id: UUID | None = None
    capture_session_id: UUID | None = None
    timestamp: datetime
    raw_value: float
    physical_value: float | None = None
    meta: dict[str, Any] = Field(default_factory=dict)
    conversion_status: TelemetryConversionStatus = TelemetryConversionStatus.RAW_ONLY
    conversion_profile_id: UUID | None = None


class RunMetricPointDTO(BaseModel):
    model_config = ConfigDict(extra="forbid")

    name: str
    step: int
    value: float
    timestamp: datetime


class RunMetricIngestDTO(BaseModel):
    model_config = ConfigDict(extra="forbid")

    project_id: UUID
    run_id: UUID
    points: list[RunMetricPointDTO] = Field(default_factory=list)

    @field_validator("points")
    @classmethod
    def _validate_points(cls, value: list[RunMetricPointDTO]) -> list[RunMetricPointDTO]:
        if not value:
            raise ValueError("metrics must not be empty")
        return value
