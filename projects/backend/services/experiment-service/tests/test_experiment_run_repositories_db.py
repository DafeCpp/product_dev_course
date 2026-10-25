"""Experiment and run repositories against a real database (plain pool: JSONB arrives as text)."""
from __future__ import annotations

import uuid
from datetime import UTC, datetime, timedelta

import asyncpg
import pytest

from experiment_service.core.exceptions import (
    DuplicateResourceError,
    EmptyUpdateError,
    NotFoundError,
)
from experiment_service.domain.dto import (
    ExperimentCreateDTO,
    ExperimentUpdateDTO,
    RunCreateDTO,
    RunUpdateDTO,
)
from experiment_service.domain.enums import ExperimentStatus, RunStatus
from experiment_service.repositories import ExperimentRepository, RunRepository


@pytest.fixture
async def db_pool(pgsql):
    pool = await asyncpg.create_pool(dsn=pgsql["experiment_service"].conninfo.get_uri())
    try:
        yield pool
    finally:
        await pool.close()


@pytest.fixture
def project_id() -> uuid.UUID:
    return uuid.uuid4()


def experiment_dto(project_id, name: str, **extra) -> ExperimentCreateDTO:
    return ExperimentCreateDTO(project_id=project_id, owner_id=uuid.uuid4(), name=name, **extra)


def run_dto(project_id, experiment_id, name: str, **extra) -> RunCreateDTO:
    return RunCreateDTO(experiment_id=experiment_id, project_id=project_id, created_by=uuid.uuid4(), name=name, **extra)


# --- experiments ------------------------------------------------------------------------------------------


async def test_experiment_create_get_and_duplicates(db_pool, project_id):
    repo = ExperimentRepository(db_pool)
    created = await repo.create(experiment_dto(project_id, "alpha", metadata={"k": 1}, tags=["x"]))
    assert created.metadata == {"k": 1}
    with pytest.raises(DuplicateResourceError):
        await repo.create(experiment_dto(project_id, "alpha"))

    assert (await repo.get(project_id, created.id)).name == "alpha"
    with pytest.raises(NotFoundError):
        await repo.get(project_id, uuid.uuid4())


async def test_experiment_listing_and_search(db_pool, project_id):
    repo = ExperimentRepository(db_pool)
    await repo.create(experiment_dto(project_id, "alpha", description="wind tunnel", tags=["x"]))
    await repo.create(experiment_dto(project_id, "beta", status=ExperimentStatus.RUNNING))

    items, total = await repo.list_by_project(project_id, limit=1)
    assert (len(items), total) == (1, 2)
    items, total = await repo.list_by_project(project_id, offset=10)
    assert (items, total) == ([], 2)
    items, total = await repo.list_by_project(project_id, status=ExperimentStatus.RUNNING, offset=5)
    assert (items, total) == ([], 1)

    items, total = await repo.search_experiments(project_id, "tunnel")
    assert ([e.name for e in items], total) == (["alpha"], 1)
    items, total = await repo.search_experiments(project_id, "tunnel", offset=10)
    assert (items, total) == ([], 1)
    items, total = await repo.search_experiments(project_id, "nothing")
    assert (items, total) == ([], 0)


async def test_experiment_update_and_delete(db_pool, project_id):
    repo = ExperimentRepository(db_pool)
    first = await repo.create(experiment_dto(project_id, "alpha"))
    await repo.create(experiment_dto(project_id, "beta"))

    with pytest.raises(EmptyUpdateError):
        await repo.update(project_id, first.id, ExperimentUpdateDTO())
    with pytest.raises(DuplicateResourceError):
        await repo.update(project_id, first.id, ExperimentUpdateDTO(name="beta"))
    with pytest.raises(NotFoundError):
        await repo.update(project_id, uuid.uuid4(), ExperimentUpdateDTO(name="gamma"))
    updated = await repo.update(project_id, first.id, ExperimentUpdateDTO(name="gamma", metadata={"a": 2}))
    assert (updated.name, updated.metadata) == ("gamma", {"a": 2})

    await repo.delete(project_id, first.id)
    with pytest.raises(NotFoundError):
        await repo.delete(project_id, first.id)


# --- runs -------------------------------------------------------------------------------------------------


async def make_experiment(db_pool, project_id):
    return await ExperimentRepository(db_pool).create(experiment_dto(project_id, f"exp-{uuid.uuid4()}"))


async def test_run_listing_and_filters(db_pool, project_id):
    experiment = await make_experiment(db_pool, project_id)
    repo = RunRepository(db_pool)
    await repo.create(run_dto(project_id, experiment.id, "a", params={"p": 1}, metadata={"m": 1}))
    await repo.create(run_dto(project_id, experiment.id, "b", status=RunStatus.RUNNING))
    now = datetime.now(UTC)

    items, total = await repo.list_by_project(project_id, limit=1)
    assert (len(items), total) == (1, 2)
    items, total = await repo.list_by_project(project_id, offset=10)
    assert (items, total) == ([], 2)

    items, total = await repo.list_by_experiment(project_id, experiment.id)
    assert total == 2
    items, total = await repo.list_by_experiment(
        project_id,
        experiment.id,
        status=RunStatus.RUNNING,
        created_after=now - timedelta(minutes=5),
        created_before=now + timedelta(minutes=5),
    )
    assert ([r.name for r in items], total) == (["b"], 1)
    items, total = await repo.list_by_experiment(project_id, experiment.id, offset=10)
    assert (items, total) == ([], 2)
    items, total = await repo.list_by_experiment(project_id, experiment.id, tags=["nope"])
    assert (items, total) == ([], 0)


async def test_run_update_and_delete(db_pool, project_id):
    experiment = await make_experiment(db_pool, project_id)
    repo = RunRepository(db_pool)
    run = await repo.create(run_dto(project_id, experiment.id, "a"))

    with pytest.raises(EmptyUpdateError):
        await repo.update(project_id, run.id, RunUpdateDTO())
    with pytest.raises(NotFoundError):
        await repo.update(project_id, uuid.uuid4(), RunUpdateDTO(name="x"))
    updated = await repo.update(project_id, run.id, RunUpdateDTO(name="renamed", params={"p": 2}))
    assert (updated.name, updated.params) == ("renamed", {"p": 2})

    await repo.delete(project_id, run.id)
    with pytest.raises(NotFoundError):
        await repo.delete(project_id, run.id)


async def test_run_batch_operations(db_pool, project_id):
    experiment = await make_experiment(db_pool, project_id)
    repo = RunRepository(db_pool)
    first = await repo.create(run_dto(project_id, experiment.id, "a"))
    second = await repo.create(run_dto(project_id, experiment.id, "b"))
    ids = [first.id, second.id]

    assert await repo.update_status_batch(project_id, [], RunStatus.RUNNING) == []
    moved = await repo.update_status_batch(project_id, ids, RunStatus.RUNNING)
    assert {r.status for r in moved} == {RunStatus.RUNNING}
    with pytest.raises(NotFoundError):
        await repo.update_status_batch(project_id, [*ids, uuid.uuid4()], RunStatus.FAILED)

    brief = await repo.fetch_runs_brief(project_id, experiment.id, ids)
    assert {item["name"] for item in brief} == {"a", "b"}

    assert await repo.bulk_update_tags(project_id, []) == []
    tagged = await repo.bulk_update_tags(project_id, ids, set_tags=["x", "y"])
    assert all(sorted(r.tags) == ["x", "y"] for r in tagged)
    changed = await repo.bulk_update_tags(project_id, ids, add_tags=["z"], remove_tags=["x"])
    assert all(sorted(r.tags) == ["y", "z"] for r in changed)
    with pytest.raises(NotFoundError):
        await repo.bulk_update_tags(project_id, [*ids, uuid.uuid4()], add_tags=["q"])


async def test_overdue_runs(db_pool, project_id):
    experiment = await make_experiment(db_pool, project_id)
    repo = RunRepository(db_pool)
    started = datetime.now(UTC) - timedelta(hours=2)
    overdue = await repo.create(
        run_dto(project_id, experiment.id, "late", status=RunStatus.RUNNING, started_at=started, auto_complete_after_minutes=30)
    )
    await repo.create(run_dto(project_id, experiment.id, "no-deadline", status=RunStatus.RUNNING, started_at=started))

    found = await repo.get_overdue_runs(datetime.now(UTC))
    assert [r.id for r in found if r.project_id == project_id] == [overdue.id]
