"\"\"\"Domain enums derived from the technical specification.\"\"\""
from __future__ import annotations

from enum import Enum


class ExperimentStatus(str, Enum):
    """Canonical experiment statuses."""

    DRAFT = "draft"
    RUNNING = "running"
    FAILED = "failed"
    SUCCEEDED = "succeeded"
    ARCHIVED = "archived"


class RunStatus(str, Enum):
    """Run lifecycle states."""

    DRAFT = "draft"
    RUNNING = "running"
    FAILED = "failed"
    SUCCEEDED = "succeeded"
    ARCHIVED = "archived"


class CaptureSessionStatus(str, Enum):
    """Capture session states."""

    DRAFT = "draft"
    RUNNING = "running"
    FAILED = "failed"
    SUCCEEDED = "succeeded"
    ARCHIVED = "archived"
    BACKFILLING = "backfilling"


class ConversionProfileStatus(str, Enum):
    """Conversion profile lifecycle."""

    DRAFT = "draft"
    ACTIVE = "active"
    SCHEDULED = "scheduled"
    DEPRECATED = "deprecated"


class SensorStatus(str, Enum):
    """Sensor lifecycle states."""

    REGISTERING = "registering"
    ACTIVE = "active"
    INACTIVE = "inactive"
    DECOMMISSIONED = "decommissioned"


