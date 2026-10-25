"""Capture session and conversion profile repositories against a real database."""
from __future__ import annotations

import uuid
from datetime import UTC, datetime, timedelta

import asyncpg
import pytest

from experiment_service.core.exceptions import (
    DuplicateResourceError,
    InvalidStatusTransitionError,
    NotFoundError,
)
from experiment_service.domain.dto import (
    CaptureSessionCreateDTO,
    CaptureSessionUpdateDTO,
    ConversionProfileCreateDTO,
    SensorCreateDTO,
)
from experiment_service.domain.enums import CaptureSessionStatus, ConversionProfileStatus
from experiment_service.repositories import SensorRepository
from experiment_service.repositories.capture_sessions import CaptureSessionRepository
from experiment_service.repositories.conversion_profiles import ConversionProfileRepository
from tests.utils import make_headers


@pytest.fixture
async def db_pool(pgsql):
    pool = await asyncpg.create_pool(dsn=pgsql["experiment_service"].conninfo.get_uri())
    try:
        yield pool
    finally:
        await pool.close()


@pytest.fixture
async def run(service_client) -> tuple[uuid.UUID, uuid.UUID]:
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    response = await service_client.post(
        "/api/v1/experiments", json={"project_id": str(project_id), "name": "exp"}, headers=headers
    )
    experiment = await response.json()
    response = await service_client.post(
        f"/api/v1/experiments/{experiment['id']}/runs", json={"name": "run"}, headers=headers
    )
    return project_id, uuid.UUID((await response.json())["id"])


def session_dto(project_id, run_id, ordinal: int, **extra) -> CaptureSessionCreateDTO:
    return CaptureSessionCreateDTO(run_id=run_id, project_id=project_id, ordinal_number=ordinal, **extra)


# --- capture sessions -------------------------------------------------------------------------------------


async def test_capture_session_crud_and_listing(db_pool, run):
    project_id, run_id = run
    repo = CaptureSessionRepository(db_pool)
    first = await repo.create(session_dto(project_id, run_id, 1))
    second = await repo.create(session_dto(project_id, run_id, 2))
    with pytest.raises(DuplicateResourceError):
        await repo.create(session_dto(project_id, run_id, 1))

    assert (await repo.get(project_id, first.id)).ordinal_number == 1
    with pytest.raises(NotFoundError):
        await repo.get(project_id, uuid.uuid4())

    items, total = await repo.list_by_project(project_id, limit=1)
    assert (len(items), total) == (1, 2)
    items, total = await repo.list_by_project(project_id, offset=10)
    assert (items, total) == ([], 2)
    items, total = await repo.list_by_run(project_id, run_id)
    assert [s.id for s in items] == [first.id, second.id] and total == 2
    items, total = await repo.list_by_run(project_id, run_id, offset=10)
    assert (items, total) == ([], 2)


async def test_capture_session_activity_flags(db_pool, run):
    project_id, run_id = run
    repo = CaptureSessionRepository(db_pool)
    assert await repo.has_active_for_run(project_id, run_id) is False
    assert await repo.has_active_for_project(project_id) is False

    session = await repo.create(session_dto(project_id, run_id, 1, status=CaptureSessionStatus.RUNNING))
    assert await repo.has_active_for_run(project_id, run_id) is True
    assert await repo.has_active_for_project(project_id) is True

    updated = await repo.update(project_id, session.id, CaptureSessionUpdateDTO(status=CaptureSessionStatus.SUCCEEDED))
    assert updated.status == CaptureSessionStatus.SUCCEEDED
    assert await repo.has_active_for_run(project_id, run_id) is False


async def test_capture_session_update_errors_and_delete(db_pool, run):
    project_id, run_id = run
    repo = CaptureSessionRepository(db_pool)
    session = await repo.create(session_dto(project_id, run_id, 1))

    with pytest.raises(ValueError):
        await repo.update(project_id, session.id, CaptureSessionUpdateDTO())
    with pytest.raises(NotFoundError):
        await repo.update(project_id, uuid.uuid4(), CaptureSessionUpdateDTO(notes="x"))

    await repo.delete(project_id, session.id)
    with pytest.raises(NotFoundError):
        await repo.delete(project_id, session.id)


async def test_fail_stale_sessions(db_pool, run):
    project_id, run_id = run
    repo = CaptureSessionRepository(db_pool)
    started = datetime.now(UTC) - timedelta(hours=5)
    stale = await repo.create(session_dto(project_id, run_id, 1, status=CaptureSessionStatus.RUNNING, started_at=started))

    failed = await repo.fail_stale_sessions(datetime.now(UTC) - timedelta(hours=1))
    assert failed == 1
    assert (await repo.get(project_id, stale.id)).status == CaptureSessionStatus.FAILED
    failed_again = await repo.fail_stale_sessions(datetime.now(UTC) - timedelta(hours=1))
    assert failed_again == 0


# --- conversion profiles ----------------------------------------------------------------------------------


async def make_sensor(db_pool, project_id):
    sensor = await SensorRepository(db_pool).create(
        SensorCreateDTO(project_id=project_id, name="s", type="t", input_unit="mV", display_unit="C"),
        token_hash=b"h",
        token_preview="hhhh",
    )
    return sensor


def profile_dto(sensor, version: str, **extra) -> ConversionProfileCreateDTO:
    return ConversionProfileCreateDTO(
        sensor_id=sensor.id,
        project_id=sensor.project_id,
        version=version,
        kind="linear",
        payload={"a": 1.0, "b": 0.0},
        created_by=uuid.uuid4(),
        **extra,
    )


async def test_profile_create_get_and_list(db_pool):
    project_id = uuid.uuid4()
    sensor = await make_sensor(db_pool, project_id)
    repo = ConversionProfileRepository(db_pool)
    first = await repo.create(profile_dto(sensor, "v1"))
    await repo.create(profile_dto(sensor, "v2"))
    with pytest.raises(DuplicateResourceError):
        await repo.create(profile_dto(sensor, "v1"))

    assert (await repo.get(project_id, sensor.id, first.id)).payload == {"a": 1.0, "b": 0.0}
    with pytest.raises(NotFoundError):
        await repo.get(project_id, sensor.id, uuid.uuid4())

    items, total = await repo.list_by_sensor(project_id, sensor.id, limit=1)
    assert (len(items), total) == (1, 2)
    items, total = await repo.list_by_sensor(project_id, sensor.id, offset=10)
    assert (items, total) == ([], 2)


async def test_profile_status_updates_and_publish(db_pool):
    project_id = uuid.uuid4()
    sensor = await make_sensor(db_pool, project_id)
    repo = ConversionProfileRepository(db_pool)
    old = await repo.create(profile_dto(sensor, "v1"))
    new = await repo.create(profile_dto(sensor, "v2"))
    user = uuid.uuid4()

    scheduled = await repo.update_status(
        project_id,
        sensor.id,
        new.id,
        status=ConversionProfileStatus.SCHEDULED,
        valid_from=datetime.now(UTC) + timedelta(days=1),
        valid_to=None,
        published_by=user,
    )
    assert scheduled.status == ConversionProfileStatus.SCHEDULED
    with pytest.raises(NotFoundError):
        await repo.update_status(
            project_id,
            sensor.id,
            uuid.uuid4(),
            status=ConversionProfileStatus.ACTIVE,
            valid_from=None,
            valid_to=None,
            published_by=user,
        )

    await repo.publish_profile(project_id, sensor.id, old.id, published_by=user, effective_from=None)
    published = await repo.publish_profile(project_id, sensor.id, new.id, published_by=user, effective_from=None)
    assert published.status == ConversionProfileStatus.ACTIVE
    assert (await repo.get(project_id, sensor.id, old.id)).status == ConversionProfileStatus.DEPRECATED
    assert (await SensorRepository(db_pool).get(project_id, sensor.id)).active_profile_id == new.id

    with pytest.raises(InvalidStatusTransitionError):
        await repo.publish_profile(project_id, sensor.id, new.id, published_by=user, effective_from=None)
    with pytest.raises(NotFoundError):
        await repo.publish_profile(project_id, sensor.id, uuid.uuid4(), published_by=user, effective_from=None)
