"""Pydantic DTOs for telemetry ingest."""
from __future__ import annotations

from datetime import datetime
from typing import Any
from uuid import UUID

from pydantic import BaseModel, ConfigDict, Field


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
    # Optional batch-level meta; merged into each reading's meta (reading wins on key conflicts)
    meta: dict[str, Any] = Field(default_factory=dict)

    # MVP limits: max 10k readings per request
    # mypy-friendly constraints for Pydantic v2
    readings: list[TelemetryReadingDTO] = Field(min_length=1, max_length=10_000)


class WsIngestMessageDTO(BaseModel):
    """Per-message payload for the WebSocket ingest endpoint.

    sensor_id is not included here — it is fixed at connection time
    via the ``sensor_id`` query parameter.
    """

    model_config = ConfigDict(extra="forbid")

    run_id: UUID | None = None
    capture_session_id: UUID | None = None
    meta: dict[str, Any] = Field(default_factory=dict)
    readings: list[TelemetryReadingDTO] = Field(min_length=1, max_length=10_000)
    # Optional sequence number echoed back in the acknowledgement for
    # client-side request/response correlation.
    seq: int | None = None

