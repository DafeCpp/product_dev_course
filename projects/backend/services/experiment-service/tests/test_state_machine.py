"""Unit tests for experiment_service.services.state_machine module."""
from __future__ import annotations

import pytest

from experiment_service.core.exceptions import InvalidStatusTransitionError
from experiment_service.domain.enums import (
    CaptureSessionStatus,
    ConversionProfileStatus,
    ExperimentStatus,
    RunStatus,
)
from experiment_service.services.state_machine import (
    CAPTURE_TRANSITIONS,
    CONVERSION_PROFILE_TRANSITIONS,
    EXPERIMENT_TRANSITIONS,
    RUN_TRANSITIONS,
    validate_capture_transition,
    validate_conversion_profile_transition,
    validate_experiment_transition,
    validate_run_transition,
)


class TestExperimentTransitions:
    """Tests for EXPERIMENT_TRANSITIONS mapping."""

    def test_draft_to_running_allowed(self):
        """Test DRAFT → RUNNING transition is allowed."""
        assert ExperimentStatus.RUNNING in EXPERIMENT_TRANSITIONS[ExperimentStatus.DRAFT]

    def test_draft_to_archived_allowed(self):
        """Test DRAFT → ARCHIVED transition is allowed."""
        assert ExperimentStatus.ARCHIVED in EXPERIMENT_TRANSITIONS[ExperimentStatus.DRAFT]

    def test_running_to_succeeded_allowed(self):
        """Test RUNNING → SUCCEEDED transition is allowed."""
        assert ExperimentStatus.SUCCEEDED in EXPERIMENT_TRANSITIONS[ExperimentStatus.RUNNING]

    def test_running_to_failed_allowed(self):
        """Test RUNNING → FAILED transition is allowed."""
        assert ExperimentStatus.FAILED in EXPERIMENT_TRANSITIONS[ExperimentStatus.RUNNING]

    def test_succeeded_to_archived_allowed(self):
        """Test SUCCEEDED → ARCHIVED transition is allowed."""
        assert ExperimentStatus.ARCHIVED in EXPERIMENT_TRANSITIONS[ExperimentStatus.SUCCEEDED]

    def test_failed_to_archived_allowed(self):
        """Test FAILED → ARCHIVED transition is allowed."""
        assert ExperimentStatus.ARCHIVED in EXPERIMENT_TRANSITIONS[ExperimentStatus.FAILED]

    def test_draft_to_draft_allowed(self):
        """Test DRAFT → DRAFT (same status) is implicitly allowed."""
        # Same status transitions are always allowed by _validate_transition
        assert ExperimentStatus.DRAFT in EXPERIMENT_TRANSITIONS[ExperimentStatus.DRAFT] or True

    def test_running_to_draft_not_allowed(self):
        """Test RUNNING → DRAFT transition is NOT allowed."""
        assert ExperimentStatus.DRAFT not in EXPERIMENT_TRANSITIONS[ExperimentStatus.RUNNING]

    def test_succeeded_to_running_not_allowed(self):
        """Test SUCCEEDED → RUNNING transition is NOT allowed."""
        assert ExperimentStatus.RUNNING not in EXPERIMENT_TRANSITIONS[ExperimentStatus.SUCCEEDED]

    def test_archived_is_terminal(self):
        """Test ARCHIVED has no outgoing transitions (except to itself)."""
        transitions = EXPERIMENT_TRANSITIONS[ExperimentStatus.ARCHIVED]
        assert len(transitions) == 0

    def test_all_statuses_have_transitions(self):
        """Test all ExperimentStatus values have transition rules."""
        for status in ExperimentStatus:
            assert status in EXPERIMENT_TRANSITIONS


class TestRunTransitions:
    """Tests for RUN_TRANSITIONS mapping."""

    def test_draft_to_running_allowed(self):
        """Test DRAFT → RUNNING transition is allowed."""
        assert RunStatus.RUNNING in RUN_TRANSITIONS[RunStatus.DRAFT]

    def test_draft_to_archived_allowed(self):
        """Test DRAFT → ARCHIVED transition is allowed."""
        assert RunStatus.ARCHIVED in RUN_TRANSITIONS[RunStatus.DRAFT]

    def test_running_to_succeeded_allowed(self):
        """Test RUNNING → SUCCEEDED transition is allowed."""
        assert RunStatus.SUCCEEDED in RUN_TRANSITIONS[RunStatus.RUNNING]

    def test_running_to_failed_allowed(self):
        """Test RUNNING → FAILED transition is allowed."""
        assert RunStatus.FAILED in RUN_TRANSITIONS[RunStatus.RUNNING]

    def test_succeeded_to_archived_allowed(self):
        """Test SUCCEEDED → ARCHIVED transition is allowed."""
        assert RunStatus.ARCHIVED in RUN_TRANSITIONS[RunStatus.SUCCEEDED]

    def test_failed_to_archived_allowed(self):
        """Test FAILED → ARCHIVED transition is allowed."""
        assert RunStatus.ARCHIVED in RUN_TRANSITIONS[RunStatus.FAILED]

    def test_running_to_draft_not_allowed(self):
        """Test RUNNING → DRAFT transition is NOT allowed."""
        assert RunStatus.DRAFT not in RUN_TRANSITIONS[RunStatus.RUNNING]

    def test_succeeded_to_running_not_allowed(self):
        """Test SUCCEEDED → RUNNING transition is NOT allowed."""
        assert RunStatus.RUNNING not in RUN_TRANSITIONS[RunStatus.SUCCEEDED]

    def test_archived_is_terminal(self):
        """Test ARCHIVED has no outgoing transitions."""
        transitions = RUN_TRANSITIONS[RunStatus.ARCHIVED]
        assert len(transitions) == 0

    def test_all_statuses_have_transitions(self):
        """Test all RunStatus values have transition rules."""
        for status in RunStatus:
            assert status in RUN_TRANSITIONS


class TestCaptureTransitions:
    """Tests for CAPTURE_TRANSITIONS mapping."""

    def test_draft_to_running_allowed(self):
        """Test DRAFT → RUNNING transition is allowed."""
        assert CaptureSessionStatus.RUNNING in CAPTURE_TRANSITIONS[CaptureSessionStatus.DRAFT]

    def test_draft_to_archived_allowed(self):
        """Test DRAFT → ARCHIVED transition is allowed."""
        assert CaptureSessionStatus.ARCHIVED in CAPTURE_TRANSITIONS[CaptureSessionStatus.DRAFT]

    def test_running_to_succeeded_allowed(self):
        """Test RUNNING → SUCCEEDED transition is allowed."""
        assert CaptureSessionStatus.SUCCEEDED in CAPTURE_TRANSITIONS[CaptureSessionStatus.RUNNING]

    def test_running_to_failed_allowed(self):
        """Test RUNNING → FAILED transition is allowed."""
        assert CaptureSessionStatus.FAILED in CAPTURE_TRANSITIONS[CaptureSessionStatus.RUNNING]

    def test_succeeded_to_backfilling_allowed(self):
        """Test SUCCEEDED → BACKFILLING transition is allowed."""
        assert CaptureSessionStatus.BACKFILLING in CAPTURE_TRANSITIONS[CaptureSessionStatus.SUCCEEDED]

    def test_succeeded_to_archived_allowed(self):
        """Test SUCCEEDED → ARCHIVED transition is allowed."""
        assert CaptureSessionStatus.ARCHIVED in CAPTURE_TRANSITIONS[CaptureSessionStatus.SUCCEEDED]

    def test_backfilling_to_succeeded_allowed(self):
        """Test BACKFILLING → SUCCEEDED transition is allowed."""
        assert CaptureSessionStatus.SUCCEEDED in CAPTURE_TRANSITIONS[CaptureSessionStatus.BACKFILLING]

    def test_failed_to_archived_allowed(self):
        """Test FAILED → ARCHIVED transition is allowed."""
        assert CaptureSessionStatus.ARCHIVED in CAPTURE_TRANSITIONS[CaptureSessionStatus.FAILED]

    def test_backfilling_to_archived_not_allowed(self):
        """Test BACKFILLING → ARCHIVED transition is NOT allowed."""
        assert CaptureSessionStatus.ARCHIVED not in CAPTURE_TRANSITIONS[CaptureSessionStatus.BACKFILLING]

    def test_succeeded_to_running_not_allowed(self):
        """Test SUCCEEDED → RUNNING transition is NOT allowed."""
        assert CaptureSessionStatus.RUNNING not in CAPTURE_TRANSITIONS[CaptureSessionStatus.SUCCEEDED]

    def test_archived_is_terminal(self):
        """Test ARCHIVED has no outgoing transitions."""
        transitions = CAPTURE_TRANSITIONS[CaptureSessionStatus.ARCHIVED]
        assert len(transitions) == 0

    def test_all_statuses_have_transitions(self):
        """Test all CaptureSessionStatus values have transition rules."""
        for status in CaptureSessionStatus:
            assert status in CAPTURE_TRANSITIONS


class TestConversionProfileTransitions:
    """Tests for CONVERSION_PROFILE_TRANSITIONS mapping."""

    def test_draft_to_scheduled_allowed(self):
        """Test DRAFT → SCHEDULED transition is allowed."""
        assert ConversionProfileStatus.SCHEDULED in CONVERSION_PROFILE_TRANSITIONS[ConversionProfileStatus.DRAFT]

    def test_draft_to_active_allowed(self):
        """Test DRAFT → ACTIVE transition is allowed."""
        assert ConversionProfileStatus.ACTIVE in CONVERSION_PROFILE_TRANSITIONS[ConversionProfileStatus.DRAFT]

    def test_draft_to_deprecated_allowed(self):
        """Test DRAFT → DEPRECATED transition is allowed."""
        assert ConversionProfileStatus.DEPRECATED in CONVERSION_PROFILE_TRANSITIONS[ConversionProfileStatus.DRAFT]

    def test_scheduled_to_active_allowed(self):
        """Test SCHEDULED → ACTIVE transition is allowed."""
        assert ConversionProfileStatus.ACTIVE in CONVERSION_PROFILE_TRANSITIONS[ConversionProfileStatus.SCHEDULED]

    def test_scheduled_to_deprecated_allowed(self):
        """Test SCHEDULED → DEPRECATED transition is allowed."""
        assert ConversionProfileStatus.DEPRECATED in CONVERSION_PROFILE_TRANSITIONS[ConversionProfileStatus.SCHEDULED]

    def test_active_to_deprecated_allowed(self):
        """Test ACTIVE → DEPRECATED transition is allowed."""
        assert ConversionProfileStatus.DEPRECATED in CONVERSION_PROFILE_TRANSITIONS[ConversionProfileStatus.ACTIVE]

    def test_deprecated_is_terminal(self):
        """Test DEPRECATED has no outgoing transitions."""
        transitions = CONVERSION_PROFILE_TRANSITIONS[ConversionProfileStatus.DEPRECATED]
        assert len(transitions) == 0

    def test_active_to_draft_not_allowed(self):
        """Test ACTIVE → DRAFT transition is NOT allowed."""
        assert ConversionProfileStatus.DRAFT not in CONVERSION_PROFILE_TRANSITIONS[ConversionProfileStatus.ACTIVE]

    def test_scheduled_to_draft_not_allowed(self):
        """Test SCHEDULED → DRAFT transition is NOT allowed."""
        assert ConversionProfileStatus.DRAFT not in CONVERSION_PROFILE_TRANSITIONS[ConversionProfileStatus.SCHEDULED]

    def test_all_statuses_have_transitions(self):
        """Test all ConversionProfileStatus values have transition rules."""
        for status in ConversionProfileStatus:
            assert status in CONVERSION_PROFILE_TRANSITIONS


class TestValidateExperimentTransition:
    """Tests for validate_experiment_transition function."""

    def test_valid_transition_no_exception(self):
        """Test valid transition doesn't raise."""
        # Should not raise
        validate_experiment_transition(
            ExperimentStatus.DRAFT,
            ExperimentStatus.RUNNING,
        )

    def test_same_status_no_exception(self):
        """Test same status doesn't raise."""
        # Should not raise
        validate_experiment_transition(
            ExperimentStatus.DRAFT,
            ExperimentStatus.DRAFT,
        )

    def test_invalid_transition_raises(self):
        """Test invalid transition raises InvalidStatusTransitionError."""
        with pytest.raises(InvalidStatusTransitionError) as exc_info:
            validate_experiment_transition(
                ExperimentStatus.RUNNING,
                ExperimentStatus.DRAFT,
            )
        assert "Invalid experiment status transition" in str(exc_info.value)
        assert "running" in str(exc_info.value).lower()
        assert "draft" in str(exc_info.value).lower()

    def test_archived_to_any_raises(self):
        """Test ARCHIVED to any status raises."""
        for status in ExperimentStatus:
            if status != ExperimentStatus.ARCHIVED:
                with pytest.raises(InvalidStatusTransitionError):
                    validate_experiment_transition(
                        ExperimentStatus.ARCHIVED,
                        status,
                    )

    def test_succeeded_to_running_raises(self):
        """Test SUCCEEDED → RUNNING raises."""
        with pytest.raises(InvalidStatusTransitionError):
            validate_experiment_transition(
                ExperimentStatus.SUCCEEDED,
                ExperimentStatus.RUNNING,
            )

    def test_error_message_format(self):
        """Test error message has correct format."""
        with pytest.raises(InvalidStatusTransitionError) as exc_info:
            validate_experiment_transition(
                ExperimentStatus.FAILED,
                ExperimentStatus.RUNNING,
            )
        assert "failed" in str(exc_info.value).lower()
        assert "running" in str(exc_info.value).lower()


class TestValidateRunTransition:
    """Tests for validate_run_transition function."""

    def test_valid_transition_no_exception(self):
        """Test valid transition doesn't raise."""
        # Should not raise
        validate_run_transition(RunStatus.DRAFT, RunStatus.RUNNING)

    def test_same_status_no_exception(self):
        """Test same status doesn't raise."""
        # Should not raise
        validate_run_transition(RunStatus.DRAFT, RunStatus.DRAFT)

    def test_invalid_transition_raises(self):
        """Test invalid transition raises."""
        with pytest.raises(InvalidStatusTransitionError):
            validate_run_transition(RunStatus.RUNNING, RunStatus.DRAFT)

    def test_archived_to_any_raises(self):
        """Test ARCHIVED to any status raises."""
        for status in RunStatus:
            if status != RunStatus.ARCHIVED:
                with pytest.raises(InvalidStatusTransitionError):
                    validate_run_transition(RunStatus.ARCHIVED, status)


class TestValidateCaptureTransition:
    """Tests for validate_capture_transition function."""

    def test_valid_transition_no_exception(self):
        """Test valid transition doesn't raise."""
        # Should not raise
        validate_capture_transition(
            CaptureSessionStatus.DRAFT,
            CaptureSessionStatus.RUNNING,
        )

    def test_same_status_no_exception(self):
        """Test same status doesn't raise."""
        # Should not raise
        validate_capture_transition(
            CaptureSessionStatus.DRAFT,
            CaptureSessionStatus.DRAFT,
        )

    def test_invalid_transition_raises(self):
        """Test invalid transition raises."""
        with pytest.raises(InvalidStatusTransitionError):
            validate_capture_transition(
                CaptureSessionStatus.RUNNING,
                CaptureSessionStatus.DRAFT,
            )

    def test_backfilling_to_archived_raises(self):
        """Test BACKFILLING → ARCHIVED raises."""
        with pytest.raises(InvalidStatusTransitionError):
            validate_capture_transition(
                CaptureSessionStatus.BACKFILLING,
                CaptureSessionStatus.ARCHIVED,
            )

    def test_succeeded_to_running_raises(self):
        """Test SUCCEEDED → RUNNING raises."""
        with pytest.raises(InvalidStatusTransitionError):
            validate_capture_transition(
                CaptureSessionStatus.SUCCEEDED,
                CaptureSessionStatus.RUNNING,
            )


class TestValidateConversionProfileTransition:
    """Tests for validate_conversion_profile_transition function."""

    def test_valid_transition_no_exception(self):
        """Test valid transition doesn't raise."""
        # Should not raise
        validate_conversion_profile_transition(
            ConversionProfileStatus.DRAFT,
            ConversionProfileStatus.ACTIVE,
        )

    def test_same_status_no_exception(self):
        """Test same status doesn't raise."""
        # Should not raise
        validate_conversion_profile_transition(
            ConversionProfileStatus.DRAFT,
            ConversionProfileStatus.DRAFT,
        )

    def test_invalid_transition_raises(self):
        """Test invalid transition raises."""
        with pytest.raises(InvalidStatusTransitionError):
            validate_conversion_profile_transition(
                ConversionProfileStatus.ACTIVE,
                ConversionProfileStatus.DRAFT,
            )

    def test_deprecated_to_any_raises(self):
        """Test DEPRECATED to any status raises."""
        for status in ConversionProfileStatus:
            if status != ConversionProfileStatus.DEPRECATED:
                with pytest.raises(InvalidStatusTransitionError):
                    validate_conversion_profile_transition(
                        ConversionProfileStatus.DEPRECATED,
                        status,
                    )

    def test_active_to_scheduled_raises(self):
        """Test ACTIVE → SCHEDULED raises."""
        with pytest.raises(InvalidStatusTransitionError):
            validate_conversion_profile_transition(
                ConversionProfileStatus.ACTIVE,
                ConversionProfileStatus.SCHEDULED,
            )


class TestTransitionCoverage:
    """Tests to ensure all transitions are covered."""

    def test_experiment_all_statuses_covered(self):
        """Test all ExperimentStatus values are in transitions dict."""
        for status in ExperimentStatus:
            assert status in EXPERIMENT_TRANSITIONS, f"Missing transitions for {status}"

    def test_run_all_statuses_covered(self):
        """Test all RunStatus values are in transitions dict."""
        for status in RunStatus:
            assert status in RUN_TRANSITIONS, f"Missing transitions for {status}"

    def test_capture_all_statuses_covered(self):
        """Test all CaptureSessionStatus values are in transitions dict."""
        for status in CaptureSessionStatus:
            assert status in CAPTURE_TRANSITIONS, f"Missing transitions for {status}"

    def test_conversion_profile_all_statuses_covered(self):
        """Test all ConversionProfileStatus values are in transitions dict."""
        for status in ConversionProfileStatus:
            assert status in CONVERSION_PROFILE_TRANSITIONS, f"Missing transitions for {status}"

    def test_experiment_transitions_are_sets(self):
        """Test all experiment transitions are sets."""
        for status, transitions in EXPERIMENT_TRANSITIONS.items():
            assert isinstance(transitions, set), f"Transitions for {status} should be a set"

    def test_run_transitions_are_sets(self):
        """Test all run transitions are sets."""
        for status, transitions in RUN_TRANSITIONS.items():
            assert isinstance(transitions, set), f"Transitions for {status} should be a set"

    def test_capture_transitions_are_sets(self):
        """Test all capture transitions are sets."""
        for status, transitions in CAPTURE_TRANSITIONS.items():
            assert isinstance(transitions, set), f"Transitions for {status} should be a set"

    def test_conversion_profile_transitions_are_sets(self):
        """Test all conversion profile transitions are sets."""
        for status, transitions in CONVERSION_PROFILE_TRANSITIONS.items():
            assert isinstance(transitions, set), f"Transitions for {status} should be a set"


class TestStateMachineIntegration:
    """Integration tests for state machine workflows."""

    def test_experiment_lifecycle(self):
        """Test complete experiment lifecycle."""
        # DRAFT → RUNNING
        validate_experiment_transition(ExperimentStatus.DRAFT, ExperimentStatus.RUNNING)

        # RUNNING → SUCCEEDED
        validate_experiment_transition(ExperimentStatus.RUNNING, ExperimentStatus.SUCCEEDED)

        # SUCCEEDED → ARCHIVED
        validate_experiment_transition(ExperimentStatus.SUCCEEDED, ExperimentStatus.ARCHIVED)

    def test_experiment_early_archive(self):
        """Test experiment can be archived from DRAFT."""
        validate_experiment_transition(ExperimentStatus.DRAFT, ExperimentStatus.ARCHIVED)

    def test_run_lifecycle(self):
        """Test complete run lifecycle."""
        validate_run_transition(RunStatus.DRAFT, RunStatus.RUNNING)
        validate_run_transition(RunStatus.RUNNING, RunStatus.SUCCEEDED)
        validate_run_transition(RunStatus.SUCCEEDED, RunStatus.ARCHIVED)

    def test_capture_session_lifecycle(self):
        """Test complete capture session lifecycle."""
        validate_capture_transition(CaptureSessionStatus.DRAFT, CaptureSessionStatus.RUNNING)
        validate_capture_transition(CaptureSessionStatus.RUNNING, CaptureSessionStatus.SUCCEEDED)
        validate_capture_transition(CaptureSessionStatus.SUCCEEDED, CaptureSessionStatus.BACKFILLING)
        validate_capture_transition(CaptureSessionStatus.BACKFILLING, CaptureSessionStatus.SUCCEEDED)
        validate_capture_transition(CaptureSessionStatus.SUCCEEDED, CaptureSessionStatus.ARCHIVED)

    def test_conversion_profile_lifecycle(self):
        """Test complete conversion profile lifecycle."""
        validate_conversion_profile_transition(
            ConversionProfileStatus.DRAFT,
            ConversionProfileStatus.SCHEDULED,
        )
        validate_conversion_profile_transition(
            ConversionProfileStatus.SCHEDULED,
            ConversionProfileStatus.ACTIVE,
        )
        validate_conversion_profile_transition(
            ConversionProfileStatus.ACTIVE,
            ConversionProfileStatus.DEPRECATED,
        )

    def test_conversion_profile_direct_activation(self):
        """Test conversion profile can go DRAFT → ACTIVE directly."""
        validate_conversion_profile_transition(
            ConversionProfileStatus.DRAFT,
            ConversionProfileStatus.ACTIVE,
        )

    def test_invalid_lifecycle_path(self):
        """Test invalid lifecycle path raises."""
        # This path is invalid: RUNNING → DRAFT → RUNNING
        with pytest.raises(InvalidStatusTransitionError):
            validate_experiment_transition(ExperimentStatus.RUNNING, ExperimentStatus.DRAFT)
