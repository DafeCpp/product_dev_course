"""Domain status transition validators."""
from __future__ import annotations

from experiment_service.core.exceptions import InvalidStatusTransitionError
from experiment_service.domain.enums import (
    CaptureSessionStatus,
    ConversionProfileStatus,
    ExperimentStatus,
    RunStatus,
)

EXPERIMENT_TRANSITIONS: dict[ExperimentStatus, set[ExperimentStatus]] = {
    ExperimentStatus.DRAFT: {ExperimentStatus.RUNNING, ExperimentStatus.ARCHIVED},
    ExperimentStatus.RUNNING: {ExperimentStatus.SUCCEEDED, ExperimentStatus.FAILED},
    ExperimentStatus.SUCCEEDED: {ExperimentStatus.ARCHIVED},
    ExperimentStatus.FAILED: {ExperimentStatus.ARCHIVED},
}

RUN_TRANSITIONS: dict[RunStatus, set[RunStatus]] = {
    RunStatus.DRAFT: {RunStatus.RUNNING, RunStatus.ARCHIVED},
    RunStatus.RUNNING: {RunStatus.SUCCEEDED, RunStatus.FAILED},
    RunStatus.SUCCEEDED: {RunStatus.ARCHIVED},
    RunStatus.FAILED: {RunStatus.ARCHIVED},
}

CAPTURE_TRANSITIONS: dict[CaptureSessionStatus, set[CaptureSessionStatus]] = {
    CaptureSessionStatus.DRAFT: {
        CaptureSessionStatus.RUNNING,
        CaptureSessionStatus.ARCHIVED,
    },
    CaptureSessionStatus.RUNNING: {
        CaptureSessionStatus.SUCCEEDED,
        CaptureSessionStatus.FAILED,
    },
    CaptureSessionStatus.SUCCEEDED: {
        CaptureSessionStatus.BACKFILLING,
        CaptureSessionStatus.ARCHIVED,
    },
    CaptureSessionStatus.BACKFILLING: {CaptureSessionStatus.SUCCEEDED},
    CaptureSessionStatus.FAILED: {CaptureSessionStatus.ARCHIVED},
}

CONVERSION_PROFILE_TRANSITIONS: dict[
    ConversionProfileStatus, set[ConversionProfileStatus]
] = {
    ConversionProfileStatus.DRAFT: {
        ConversionProfileStatus.SCHEDULED,
        ConversionProfileStatus.ACTIVE,
        ConversionProfileStatus.DEPRECATED,
    },
    ConversionProfileStatus.SCHEDULED: {
        ConversionProfileStatus.ACTIVE,
        ConversionProfileStatus.DEPRECATED,
    },
    ConversionProfileStatus.ACTIVE: {ConversionProfileStatus.DEPRECATED},
    ConversionProfileStatus.DEPRECATED: set(),
}


def _validate_transition(entity: str, current, new, transitions: dict) -> None:
    if current == new:
        return
    allowed = transitions.get(current, set())
    if new not in allowed:
        raise InvalidStatusTransitionError(
            f"Invalid {entity} status transition: {current.value} → {new.value}"
        )


def validate_experiment_transition(current: ExperimentStatus, new: ExperimentStatus) -> None:
    _validate_transition("experiment", current, new, EXPERIMENT_TRANSITIONS)


def validate_run_transition(current: RunStatus, new: RunStatus) -> None:
    _validate_transition("run", current, new, RUN_TRANSITIONS)


def validate_capture_transition(
    current: CaptureSessionStatus, new: CaptureSessionStatus
) -> None:
    _validate_transition("capture session", current, new, CAPTURE_TRANSITIONS)


def validate_conversion_profile_transition(
    current: ConversionProfileStatus, new: ConversionProfileStatus
) -> None:
    _validate_transition("conversion profile", current, new, CONVERSION_PROFILE_TRANSITIONS)



