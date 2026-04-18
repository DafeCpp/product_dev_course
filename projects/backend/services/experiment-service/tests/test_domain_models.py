"""Tests for domain Pydantic models validation and behavior."""
import uuid
from datetime import datetime, timedelta, timezone

import pytest
from pydantic import ValidationError

from experiment_service.domain.dto import (
    CaptureSessionCreateDTO,
    ConversionProfileCreateDTO,
    ExperimentCreateDTO,
    ExperimentUpdateDTO,
    RunCreateDTO,
    RunMetricIngestDTO,
    RunMetricPointDTO,
    RunUpdateDTO,
    SensorCreateDTO,
    TelemetryIngestDTO,
    TelemetryReadingDTO,
)
from experiment_service.domain.enums import (
    CaptureSessionStatus,
    ConversionProfileStatus,
    ExperimentStatus,
    RunStatus,
    SensorStatus,
    TelemetryConversionStatus,
)
from experiment_service.domain.models import (
    Artifact,
    CaptureSession,
    ConversionProfile,
    Experiment,
    Run,
    RunEvent,
    RunMetric,
    Sensor,
    TelemetryRecord,
)


class TestExperimentModel:
    """Tests for Experiment domain model."""

    def test_create_valid_experiment(self):
        now = datetime.now(timezone.utc)
        exp = Experiment(
            id=uuid.uuid4(),
            project_id=uuid.uuid4(),
            name="Test Experiment",
            description="Test description",
            experiment_type="A/B",
            tags=["tag1", "tag2"],
            metadata={"key": "value"},
            status=ExperimentStatus.DRAFT,
            owner_id=uuid.uuid4(),
            created_at=now,
            updated_at=now,
        )
        assert exp.name == "Test Experiment"
        assert exp.status == ExperimentStatus.DRAFT
        assert len(exp.tags) == 2
        assert exp.metadata == {"key": "value"}

    def test_experiment_default_values(self):
        now = datetime.now(timezone.utc)
        exp = Experiment(
            id=uuid.uuid4(),
            project_id=uuid.uuid4(),
            name="Minimal",
            owner_id=uuid.uuid4(),
            created_at=now,
            updated_at=now,
        )
        assert exp.description is None
        assert exp.experiment_type is None
        assert exp.tags == []
        assert exp.metadata == {}
        assert exp.status == ExperimentStatus.DRAFT
        assert exp.archived_at is None

    def test_experiment_requires_name(self):
        # Pydantic allows empty strings by default, but we can test that name is required
        # by checking that omitting it entirely raises an error
        with pytest.raises(ValidationError):
            Experiment(
                id=uuid.uuid4(),
                project_id=uuid.uuid4(),
                # name is required
                owner_id=uuid.uuid4(),
                created_at=datetime.now(timezone.utc),
                updated_at=datetime.now(timezone.utc),
            )


class TestRunModel:
    """Tests for Run domain model."""

    def test_create_valid_run(self):
        now = datetime.now(timezone.utc)
        run = Run(
            id=uuid.uuid4(),
            experiment_id=uuid.uuid4(),
            project_id=uuid.uuid4(),
            created_by=uuid.uuid4(),
            name="Test Run",
            params={"lr": 0.001, "batch_size": 32},
            git_sha="abc123",
            env="staging",
            status=RunStatus.RUNNING,
            started_at=now,
            created_at=now,
            updated_at=now,
        )
        assert run.name == "Test Run"
        assert run.status == RunStatus.RUNNING
        assert run.params["lr"] == 0.001
        assert run.git_sha == "abc123"

    def test_run_default_values(self):
        run = Run(
            id=uuid.uuid4(),
            experiment_id=uuid.uuid4(),
            project_id=uuid.uuid4(),
            created_by=uuid.uuid4(),
            params={},
            created_at=datetime.now(timezone.utc),
            updated_at=datetime.now(timezone.utc),
        )
        assert run.name is None
        assert run.status == RunStatus.DRAFT
        assert run.git_sha is None
        assert run.env is None
        assert run.started_at is None
        assert run.finished_at is None
        assert run.duration_seconds is None


class TestCaptureSessionModel:
    """Tests for CaptureSession domain model."""

    def test_create_valid_capture_session(self):
        now = datetime.now(timezone.utc)
        session = CaptureSession(
            id=uuid.uuid4(),
            run_id=uuid.uuid4(),
            project_id=uuid.uuid4(),
            ordinal_number=1,
            status=CaptureSessionStatus.RUNNING,
            initiated_by=uuid.uuid4(),
            notes="Test session",
            started_at=now,
            created_at=now,
            updated_at=now,
        )
        assert session.ordinal_number == 1
        assert session.status == CaptureSessionStatus.RUNNING
        assert session.archived is False

    def test_capture_session_default_values(self):
        now = datetime.now(timezone.utc)
        session = CaptureSession(
            id=uuid.uuid4(),
            run_id=uuid.uuid4(),
            project_id=uuid.uuid4(),
            ordinal_number=1,
            created_at=now,
            updated_at=now,
        )
        assert session.status == CaptureSessionStatus.DRAFT
        assert session.initiated_by is None
        assert session.notes is None
        assert session.archived is False
        assert session.started_at is None
        assert session.stopped_at is None


class TestSensorModel:
    """Tests for Sensor domain model."""

    def test_create_valid_sensor(self):
        now = datetime.now(timezone.utc)
        sensor = Sensor(
            id=uuid.uuid4(),
            project_id=uuid.uuid4(),
            name="Temperature Sensor",
            type="DHT22",
            input_unit="celsius",
            display_unit="°C",
            status=SensorStatus.ACTIVE,
            created_at=now,
            updated_at=now,
        )
        assert sensor.name == "Temperature Sensor"
        assert sensor.type == "DHT22"
        assert sensor.status == SensorStatus.ACTIVE

    def test_sensor_default_values(self):
        now = datetime.now(timezone.utc)
        sensor = Sensor(
            id=uuid.uuid4(),
            project_id=uuid.uuid4(),
            name="Sensor",
            type="type",
            input_unit="unit",
            display_unit="unit",
            created_at=now,
            updated_at=now,
        )
        assert sensor.status == SensorStatus.REGISTERING
        assert sensor.token_preview is None
        assert sensor.last_heartbeat is None
        assert sensor.active_profile_id is None


class TestConversionProfileModel:
    """Tests for ConversionProfile domain model."""

    def test_create_valid_conversion_profile(self):
        now = datetime.now(timezone.utc)
        profile = ConversionProfile(
            id=uuid.uuid4(),
            sensor_id=uuid.uuid4(),
            project_id=uuid.uuid4(),
            version="1.0.0",
            kind="linear",
            payload={"slope": 1.5, "intercept": 0.5},
            status=ConversionProfileStatus.ACTIVE,
            created_by=uuid.uuid4(),
            created_at=now,
            updated_at=now,
        )
        assert profile.version == "1.0.0"
        assert profile.kind == "linear"
        assert profile.status == ConversionProfileStatus.ACTIVE

    def test_conversion_profile_default_values(self):
        now = datetime.now(timezone.utc)
        profile = ConversionProfile(
            id=uuid.uuid4(),
            sensor_id=uuid.uuid4(),
            project_id=uuid.uuid4(),
            version="1.0.0",
            kind="linear",
            payload={},
            created_by=uuid.uuid4(),
            created_at=now,
            updated_at=now,
        )
        assert profile.status == ConversionProfileStatus.DRAFT
        assert profile.valid_from is None
        assert profile.valid_to is None
        assert profile.published_by is None


class TestTelemetryRecordModel:
    """Tests for TelemetryRecord domain model."""

    def test_create_valid_telemetry_record(self):
        now = datetime.now(timezone.utc)
        record = TelemetryRecord(
            id=1,
            project_id=uuid.uuid4(),
            sensor_id=uuid.uuid4(),
            timestamp=now,
            raw_value=25.5,
            physical_value=25.5,
            meta={"quality": "good"},
            conversion_status=TelemetryConversionStatus.CONVERTED,
            ingested_at=now,
        )
        assert record.raw_value == 25.5
        assert record.conversion_status == TelemetryConversionStatus.CONVERTED

    def test_telemetry_record_default_values(self):
        now = datetime.now(timezone.utc)
        record = TelemetryRecord(
            id=1,
            project_id=uuid.uuid4(),
            sensor_id=uuid.uuid4(),
            timestamp=now,
            raw_value=10.0,
            ingested_at=now,
        )
        assert record.physical_value is None
        assert record.meta == {}
        assert record.conversion_status == TelemetryConversionStatus.RAW_ONLY
        assert record.run_id is None
        assert record.capture_session_id is None


class TestArtifactModel:
    """Tests for Artifact domain model."""

    def test_create_valid_artifact(self):
        now = datetime.now(timezone.utc)
        artifact = Artifact(
            id=uuid.uuid4(),
            run_id=uuid.uuid4(),
            project_id=uuid.uuid4(),
            type="model",
            uri="s3://bucket/model.pkl",
            checksum="sha256:abc123",
            size_bytes=1024000,
            metadata={"format": "pickle"},
            created_by=uuid.uuid4(),
            created_at=now,
            updated_at=now,
        )
        assert artifact.type == "model"
        assert artifact.size_bytes == 1024000
        assert artifact.is_restricted is False

    def test_artifact_default_values(self):
        now = datetime.now(timezone.utc)
        artifact = Artifact(
            id=uuid.uuid4(),
            run_id=uuid.uuid4(),
            project_id=uuid.uuid4(),
            type="log",
            uri="s3://bucket/log.txt",
            created_by=uuid.uuid4(),
            created_at=now,
            updated_at=now,
        )
        assert artifact.checksum is None
        assert artifact.size_bytes is None
        assert artifact.metadata == {}
        assert artifact.approved_by is None
        assert artifact.approval_note is None
        assert artifact.is_restricted is False


class TestExperimentCreateDTO:
    """Tests for ExperimentCreateDTO."""

    def test_valid_dto(self):
        dto = ExperimentCreateDTO(
            project_id=uuid.uuid4(),
            owner_id=uuid.uuid4(),
            name="Test",
        )
        assert dto.status == ExperimentStatus.DRAFT
        assert dto.tags == []
        assert dto.metadata == {}

    def test_extra_fields_forbidden(self):
        with pytest.raises(ValidationError):
            ExperimentCreateDTO(
                project_id=uuid.uuid4(),
                owner_id=uuid.uuid4(),
                name="Test",
                extra_field="forbidden",  # type: ignore
            )


class TestRunCreateDTO:
    """Tests for RunCreateDTO."""

    def test_valid_dto(self):
        dto = RunCreateDTO(
            experiment_id=uuid.uuid4(),
            project_id=uuid.uuid4(),
            created_by=uuid.uuid4(),
            params={"lr": 0.001},
        )
        assert dto.status == RunStatus.DRAFT
        assert dto.params == {"lr": 0.001}

    def test_params_default_empty_dict(self):
        dto = RunCreateDTO(
            experiment_id=uuid.uuid4(),
            project_id=uuid.uuid4(),
            created_by=uuid.uuid4(),
        )
        assert dto.params == {}


class TestCaptureSessionCreateDTO:
    """Tests for CaptureSessionCreateDTO."""

    def test_valid_dto(self):
        dto = CaptureSessionCreateDTO(
            run_id=uuid.uuid4(),
            project_id=uuid.uuid4(),
            ordinal_number=1,
        )
        assert dto.status == CaptureSessionStatus.DRAFT
        assert dto.ordinal_number == 1
        assert dto.archived is False


class TestSensorCreateDTO:
    """Tests for SensorCreateDTO."""

    def test_valid_dto(self):
        dto = SensorCreateDTO(
            project_id=uuid.uuid4(),
            name="Sensor",
            type="type",
            input_unit="unit",
            display_unit="unit",
        )
        assert dto.status == SensorStatus.REGISTERING


class TestTelemetryIngestDTO:
    """Tests for TelemetryIngestDTO."""

    def test_valid_dto(self):
        now = datetime.now(timezone.utc)
        dto = TelemetryIngestDTO(
            sensor_id=uuid.uuid4(),
            readings=[
                TelemetryReadingDTO(timestamp=now, raw_value=10.0),
                TelemetryReadingDTO(timestamp=now, raw_value=20.0),
            ],
        )
        assert len(dto.readings) == 2

    def test_empty_readings_raises(self):
        with pytest.raises(ValidationError, match="readings must not be empty"):
            TelemetryIngestDTO(
                sensor_id=uuid.uuid4(),
                readings=[],
            )


class TestRunMetricIngestDTO:
    """Tests for RunMetricIngestDTO."""

    def test_valid_dto(self):
        now = datetime.now(timezone.utc)
        dto = RunMetricIngestDTO(
            project_id=uuid.uuid4(),
            run_id=uuid.uuid4(),
            points=[
                RunMetricPointDTO(name="loss", step=1, value=0.5, timestamp=now),
            ],
        )
        assert len(dto.points) == 1

    def test_empty_points_raises(self):
        with pytest.raises(ValidationError, match="metrics must not be empty"):
            RunMetricIngestDTO(
                project_id=uuid.uuid4(),
                run_id=uuid.uuid4(),
                points=[],
            )


class TestConversionProfileCreateDTO:
    """Tests for ConversionProfileCreateDTO."""

    def test_linear_payload_valid(self):
        # Linear payload requires 'a' and 'b' coefficients: y = a*x + b
        dto = ConversionProfileCreateDTO(
            sensor_id=uuid.uuid4(),
            project_id=uuid.uuid4(),
            created_by=uuid.uuid4(),
            version="1.0.0",
            kind="linear",
            payload={"a": 1.5, "b": 0.5},
        )
        assert dto.kind == "linear"

    def test_polynomial_payload_valid(self):
        dto = ConversionProfileCreateDTO(
            sensor_id=uuid.uuid4(),
            project_id=uuid.uuid4(),
            created_by=uuid.uuid4(),
            version="1.0.0",
            kind="polynomial",
            payload={"coefficients": [1, 2, 3]},
        )
        assert dto.kind == "polynomial"

    def test_lookup_table_payload_valid(self):
        # lookup_table requires 'table' with at least 2 points
        dto = ConversionProfileCreateDTO(
            sensor_id=uuid.uuid4(),
            project_id=uuid.uuid4(),
            created_by=uuid.uuid4(),
            version="1.0.0",
            kind="lookup_table",
            payload={"table": [{"raw": 0, "physical": 0}, {"raw": 10, "physical": 100}]},
        )
        assert dto.kind == "lookup_table"

    def test_invalid_linear_payload_raises(self):
        with pytest.raises(ValueError, match="'a'"):
            ConversionProfileCreateDTO(
                sensor_id=uuid.uuid4(),
                project_id=uuid.uuid4(),
                created_by=uuid.uuid4(),
                version="1.0.0",
                kind="linear",
                payload={"b": 0.5},  # missing 'a'
            )

    def test_version_max_length(self):
        with pytest.raises(ValidationError):
            ConversionProfileCreateDTO(
                sensor_id=uuid.uuid4(),
                project_id=uuid.uuid4(),
                created_by=uuid.uuid4(),
                version="a" * 51,  # exceeds max_length=50
                kind="linear",
                payload={"slope": 1.0, "intercept": 0.0},
            )

    def test_version_min_length(self):
        with pytest.raises(ValidationError):
            ConversionProfileCreateDTO(
                sensor_id=uuid.uuid4(),
                project_id=uuid.uuid4(),
                created_by=uuid.uuid4(),
                version="",  # empty string
                kind="linear",
                payload={"slope": 1.0, "intercept": 0.0},
            )
