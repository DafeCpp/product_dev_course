"""Unit tests for sensor connection status and heartbeat history."""
from __future__ import annotations

import uuid
from datetime import datetime, timezone
from typing import Any
from unittest.mock import AsyncMock, MagicMock, patch

import pytest

from experiment_service.domain.enums import ConnectionStatus, SensorStatus
from experiment_service.domain.models import Sensor
from experiment_service.repositories.sensors import SensorRepository
from experiment_service.services.sensors import SensorService


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_sensor(
    *,
    sensor_id: uuid.UUID | None = None,
    project_id: uuid.UUID | None = None,
    connection_status: ConnectionStatus = ConnectionStatus.OFFLINE,
    last_heartbeat: datetime | None = None,
) -> Sensor:
    now = datetime.now(tz=timezone.utc)
    return Sensor(
        id=sensor_id or uuid.uuid4(),
        project_id=project_id or uuid.uuid4(),
        name="test-sensor",
        type="thermocouple",
        input_unit="mV",
        display_unit="C",
        status=SensorStatus.ACTIVE,
        connection_status=connection_status,
        last_heartbeat=last_heartbeat,
        created_at=now,
        updated_at=now,
    )


def _make_service(
    *,
    sensor_repo: SensorRepository | None = None,
) -> SensorService:
    if sensor_repo is None:
        sensor_repo = MagicMock(spec=SensorRepository)
    profile_repo = MagicMock()
    return SensorService(sensor_repo, profile_repo)


# ---------------------------------------------------------------------------
# test_connection_status_* — verify the enum values on Sensor model
# ---------------------------------------------------------------------------

def test_connection_status_online() -> None:
    sensor = _make_sensor(connection_status=ConnectionStatus.ONLINE)
    assert sensor.connection_status == ConnectionStatus.ONLINE
    assert sensor.connection_status.value == "online"


def test_connection_status_delayed() -> None:
    sensor = _make_sensor(connection_status=ConnectionStatus.DELAYED)
    assert sensor.connection_status == ConnectionStatus.DELAYED
    assert sensor.connection_status.value == "delayed"


def test_connection_status_offline() -> None:
    sensor = _make_sensor(connection_status=ConnectionStatus.OFFLINE)
    assert sensor.connection_status == ConnectionStatus.OFFLINE
    assert sensor.connection_status.value == "offline"


def test_connection_status_null_defaults_to_offline() -> None:
    """Sensor with no last_heartbeat should default to offline."""
    sensor = _make_sensor(last_heartbeat=None)
    assert sensor.connection_status == ConnectionStatus.OFFLINE


# ---------------------------------------------------------------------------
# test_sensor_list_includes_connection_status
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_sensor_list_includes_connection_status() -> None:
    project_id = uuid.uuid4()
    sensors = [
        _make_sensor(project_id=project_id, connection_status=ConnectionStatus.ONLINE),
        _make_sensor(project_id=project_id, connection_status=ConnectionStatus.DELAYED),
        _make_sensor(project_id=project_id, connection_status=ConnectionStatus.OFFLINE),
    ]
    repo = MagicMock(spec=SensorRepository)
    repo.list_by_project = AsyncMock(return_value=(sensors, 3))
    service = _make_service(sensor_repo=repo)

    result, total = await service.list_sensors(project_id)
    assert total == 3
    statuses = {s.connection_status for s in result}
    assert ConnectionStatus.ONLINE in statuses
    assert ConnectionStatus.DELAYED in statuses
    assert ConnectionStatus.OFFLINE in statuses


# ---------------------------------------------------------------------------
# test_status_summary_counts
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_status_summary_counts() -> None:
    project_id = uuid.uuid4()
    expected = {"online": 5, "delayed": 2, "offline": 3, "total": 10}

    repo = MagicMock(spec=SensorRepository)
    repo.get_status_summary = AsyncMock(return_value=expected)
    service = _make_service(sensor_repo=repo)

    summary = await service.get_status_summary(project_id)
    repo.get_status_summary.assert_awaited_once_with(project_id)
    assert summary["online"] == 5
    assert summary["delayed"] == 2
    assert summary["offline"] == 3
    assert summary["total"] == 10


@pytest.mark.asyncio
async def test_status_summary_all_offline() -> None:
    project_id = uuid.uuid4()
    expected = {"online": 0, "delayed": 0, "offline": 7, "total": 7}

    repo = MagicMock(spec=SensorRepository)
    repo.get_status_summary = AsyncMock(return_value=expected)
    service = _make_service(sensor_repo=repo)

    summary = await service.get_status_summary(project_id)
    assert summary["online"] == 0
    assert summary["total"] == 7


@pytest.mark.asyncio
async def test_status_summary_empty_project() -> None:
    project_id = uuid.uuid4()
    expected = {"online": 0, "delayed": 0, "offline": 0, "total": 0}

    repo = MagicMock(spec=SensorRepository)
    repo.get_status_summary = AsyncMock(return_value=expected)
    service = _make_service(sensor_repo=repo)

    summary = await service.get_status_summary(project_id)
    assert summary == {"online": 0, "delayed": 0, "offline": 0, "total": 0}


# ---------------------------------------------------------------------------
# test_heartbeat_history
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_heartbeat_history_returns_timestamps() -> None:
    sensor_id = uuid.uuid4()
    now = datetime.now(tz=timezone.utc)
    timestamps = [
        datetime(2024, 1, 1, 0, i, 0, tzinfo=timezone.utc)
        for i in range(10)
    ]

    repo = MagicMock(spec=SensorRepository)
    repo.get_heartbeat_history = AsyncMock(return_value=timestamps)
    service = _make_service(sensor_repo=repo)

    result = await service.get_heartbeat_history(sensor_id, minutes=60)
    repo.get_heartbeat_history.assert_awaited_once_with(sensor_id, 60)
    assert result == timestamps
    assert len(result) == 10


@pytest.mark.asyncio
async def test_heartbeat_history_empty() -> None:
    sensor_id = uuid.uuid4()
    repo = MagicMock(spec=SensorRepository)
    repo.get_heartbeat_history = AsyncMock(return_value=[])
    service = _make_service(sensor_repo=repo)

    result = await service.get_heartbeat_history(sensor_id, minutes=60)
    assert result == []


@pytest.mark.asyncio
async def test_heartbeat_history_respects_minutes_param() -> None:
    sensor_id = uuid.uuid4()
    repo = MagicMock(spec=SensorRepository)
    repo.get_heartbeat_history = AsyncMock(return_value=[])
    service = _make_service(sensor_repo=repo)

    await service.get_heartbeat_history(sensor_id, minutes=30)
    repo.get_heartbeat_history.assert_awaited_once_with(sensor_id, 30)
