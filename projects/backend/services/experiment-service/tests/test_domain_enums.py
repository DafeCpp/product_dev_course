"""Tests for domain enums."""
import pytest

from experiment_service.domain.enums import (
    CaptureSessionStatus,
    ConversionProfileStatus,
    ExperimentStatus,
    RunStatus,
    SensorStatus,
    TelemetryConversionStatus,
)


class TestExperimentStatus:
    """Tests for ExperimentStatus enum."""

    def test_has_expected_values(self):
        assert ExperimentStatus.DRAFT.value == "draft"
        assert ExperimentStatus.RUNNING.value == "running"
        assert ExperimentStatus.FAILED.value == "failed"
        assert ExperimentStatus.SUCCEEDED.value == "succeeded"
        assert ExperimentStatus.ARCHIVED.value == "archived"

    def test_is_str_enum(self):
        # Should be comparable to strings
        assert ExperimentStatus.DRAFT == "draft"
        assert "running" == ExperimentStatus.RUNNING

    def test_from_string(self):
        assert ExperimentStatus("draft") == ExperimentStatus.DRAFT
        assert ExperimentStatus("running") == ExperimentStatus.RUNNING
        assert ExperimentStatus("failed") == ExperimentStatus.FAILED
        assert ExperimentStatus("succeeded") == ExperimentStatus.SUCCEEDED
        assert ExperimentStatus("archived") == ExperimentStatus.ARCHIVED

    def test_invalid_value_raises(self):
        with pytest.raises(ValueError):
            ExperimentStatus("invalid_status")

    def test_all_members(self):
        members = list(ExperimentStatus)
        assert len(members) == 5
        assert ExperimentStatus.DRAFT in members
        assert ExperimentStatus.RUNNING in members
        assert ExperimentStatus.FAILED in members
        assert ExperimentStatus.SUCCEEDED in members
        assert ExperimentStatus.ARCHIVED in members


class TestRunStatus:
    """Tests for RunStatus enum."""

    def test_has_expected_values(self):
        assert RunStatus.DRAFT.value == "draft"
        assert RunStatus.RUNNING.value == "running"
        assert RunStatus.FAILED.value == "failed"
        assert RunStatus.SUCCEEDED.value == "succeeded"
        assert RunStatus.ARCHIVED.value == "archived"

    def test_is_str_enum(self):
        assert RunStatus.DRAFT == "draft"
        assert "running" == RunStatus.RUNNING

    def test_from_string(self):
        assert RunStatus("draft") == RunStatus.DRAFT
        assert RunStatus("running") == RunStatus.RUNNING

    def test_invalid_value_raises(self):
        with pytest.raises(ValueError):
            RunStatus("invalid")

    def test_all_members(self):
        members = list(RunStatus)
        assert len(members) == 5


class TestCaptureSessionStatus:
    """Tests for CaptureSessionStatus enum."""

    def test_has_expected_values(self):
        assert CaptureSessionStatus.DRAFT.value == "draft"
        assert CaptureSessionStatus.RUNNING.value == "running"
        assert CaptureSessionStatus.FAILED.value == "failed"
        assert CaptureSessionStatus.SUCCEEDED.value == "succeeded"
        assert CaptureSessionStatus.ARCHIVED.value == "archived"
        assert CaptureSessionStatus.BACKFILLING.value == "backfilling"

    def test_is_str_enum(self):
        assert CaptureSessionStatus.DRAFT == "draft"
        assert "backfilling" == CaptureSessionStatus.BACKFILLING

    def test_from_string(self):
        assert CaptureSessionStatus("draft") == CaptureSessionStatus.DRAFT
        assert CaptureSessionStatus("running") == CaptureSessionStatus.RUNNING
        assert CaptureSessionStatus("backfilling") == CaptureSessionStatus.BACKFILLING

    def test_invalid_value_raises(self):
        with pytest.raises(ValueError):
            CaptureSessionStatus("invalid")

    def test_all_members(self):
        members = list(CaptureSessionStatus)
        assert len(members) == 6


class TestConversionProfileStatus:
    """Tests for ConversionProfileStatus enum."""

    def test_has_expected_values(self):
        assert ConversionProfileStatus.DRAFT.value == "draft"
        assert ConversionProfileStatus.ACTIVE.value == "active"
        assert ConversionProfileStatus.SCHEDULED.value == "scheduled"
        assert ConversionProfileStatus.DEPRECATED.value == "deprecated"

    def test_is_str_enum(self):
        assert ConversionProfileStatus.DRAFT == "draft"
        assert "active" == ConversionProfileStatus.ACTIVE

    def test_from_string(self):
        assert ConversionProfileStatus("draft") == ConversionProfileStatus.DRAFT
        assert ConversionProfileStatus("active") == ConversionProfileStatus.ACTIVE
        assert ConversionProfileStatus("scheduled") == ConversionProfileStatus.SCHEDULED
        assert ConversionProfileStatus("deprecated") == ConversionProfileStatus.DEPRECATED

    def test_invalid_value_raises(self):
        with pytest.raises(ValueError):
            ConversionProfileStatus("invalid")

    def test_all_members(self):
        members = list(ConversionProfileStatus)
        assert len(members) == 4


class TestSensorStatus:
    """Tests for SensorStatus enum."""

    def test_has_expected_values(self):
        assert SensorStatus.REGISTERING.value == "registering"
        assert SensorStatus.ACTIVE.value == "active"
        assert SensorStatus.INACTIVE.value == "inactive"
        assert SensorStatus.DECOMMISSIONED.value == "decommissioned"

    def test_is_str_enum(self):
        assert SensorStatus.REGISTERING == "registering"
        assert "active" == SensorStatus.ACTIVE

    def test_from_string(self):
        assert SensorStatus("registering") == SensorStatus.REGISTERING
        assert SensorStatus("active") == SensorStatus.ACTIVE
        assert SensorStatus("inactive") == SensorStatus.INACTIVE
        assert SensorStatus("decommissioned") == SensorStatus.DECOMMISSIONED

    def test_invalid_value_raises(self):
        with pytest.raises(ValueError):
            SensorStatus("invalid")

    def test_all_members(self):
        members = list(SensorStatus)
        assert len(members) == 4


class TestTelemetryConversionStatus:
    """Tests for TelemetryConversionStatus enum."""

    def test_has_expected_values(self):
        assert TelemetryConversionStatus.RAW_ONLY.value == "raw_only"
        assert TelemetryConversionStatus.CONVERTED.value == "converted"
        assert TelemetryConversionStatus.CLIENT_PROVIDED.value == "client_provided"
        assert TelemetryConversionStatus.CONVERSION_FAILED.value == "conversion_failed"

    def test_is_str_enum(self):
        assert TelemetryConversionStatus.RAW_ONLY == "raw_only"
        assert "converted" == TelemetryConversionStatus.CONVERTED

    def test_from_string(self):
        assert TelemetryConversionStatus("raw_only") == TelemetryConversionStatus.RAW_ONLY
        assert TelemetryConversionStatus("converted") == TelemetryConversionStatus.CONVERTED
        assert (
            TelemetryConversionStatus("client_provided")
            == TelemetryConversionStatus.CLIENT_PROVIDED
        )
        assert (
            TelemetryConversionStatus("conversion_failed")
            == TelemetryConversionStatus.CONVERSION_FAILED
        )

    def test_invalid_value_raises(self):
        with pytest.raises(ValueError):
            TelemetryConversionStatus("invalid")

    def test_all_members(self):
        members = list(TelemetryConversionStatus)
        assert len(members) == 4


class TestEnumComparisons:
    """Tests for enum comparisons and usage in collections."""

    def test_experiment_status_in_set(self):
        statuses = {ExperimentStatus.DRAFT, ExperimentStatus.RUNNING}
        assert ExperimentStatus.DRAFT in statuses
        assert "draft" in statuses  # str comparison works

    def test_run_status_in_list(self):
        active_statuses = [RunStatus.RUNNING, RunStatus.SUCCEEDED]
        assert RunStatus.RUNNING in active_statuses
        assert "running" in active_statuses

    def test_status_equality(self):
        assert ExperimentStatus.DRAFT == ExperimentStatus.DRAFT
        assert ExperimentStatus.DRAFT != ExperimentStatus.RUNNING
        assert ExperimentStatus.DRAFT == "draft"
        assert ExperimentStatus.DRAFT != "running"

    def test_enum_hash_in_dict(self):
        status_map = {
            ExperimentStatus.DRAFT: "Creating",
            ExperimentStatus.RUNNING: "Executing",
        }
        assert status_map[ExperimentStatus.DRAFT] == "Creating"
        assert status_map["draft"] == "Creating"  # type: ignore

    def test_enum_name_attribute(self):
        assert ExperimentStatus.DRAFT.name == "DRAFT"
        assert ExperimentStatus.RUNNING.name == "RUNNING"
