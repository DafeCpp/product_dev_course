from __future__ import annotations

import uuid

import asyncpg
import pytest

from experiment_service.core.exceptions import InvalidStatusTransitionError, NotFoundError
from experiment_service.domain.dto import (
    CaptureSessionCreateDTO,
    CaptureSessionUpdateDTO,
    ExperimentCreateDTO,
    ExperimentUpdateDTO,
    RunCreateDTO,
    RunUpdateDTO,
)
from experiment_service.domain.enums import CaptureSessionStatus, ExperimentStatus, RunStatus
from experiment_service.repositories import (
    CaptureSessionRepository,
    ExperimentRepository,
    RunRepository,
)
from experiment_service.services import (
    CaptureSessionService,
    ExperimentService,
    RunService,
)


@pytest.fixture
async def db_pool(pgsql):
    conninfo = pgsql["experiment_service"].conninfo
    pool = await asyncpg.create_pool(dsn=conninfo.get_uri())
    try:
        yield pool
    finally:
        await pool.close()


@pytest.mark.asyncio
async def test_storage_layer_crud(db_pool):
    experiments_repo = ExperimentRepository(db_pool)
    runs_repo = RunRepository(db_pool)
    sessions_repo = CaptureSessionRepository(db_pool)

    experiments_service = ExperimentService(experiments_repo)
    runs_service = RunService(runs_repo, experiments_repo, sessions_repo)
    sessions_service = CaptureSessionService(sessions_repo, runs_repo)

    project_id = uuid.uuid4()
    owner_id = uuid.uuid4()

    exp = await experiments_service.create_experiment(
        ExperimentCreateDTO(
            project_id=project_id,
            owner_id=owner_id,
            name="Test Experiment",
            description="Initial description",
            experiment_type="baseline",
            tags=["alpha"],
            metadata={"priority": "p1"},
            status=ExperimentStatus.DRAFT,
        )
    )
    assert exp.name == "Test Experiment"

    fetched = await experiments_service.get_experiment(project_id, exp.id)
    assert fetched.id == exp.id

    listed, total = await experiments_service.list_experiments(project_id)
    assert len(listed) == 1
    assert total == 1

    exp = await experiments_service.update_experiment(
        project_id,
        exp.id,
        ExperimentUpdateDTO(name="Updated Experiment", tags=["alpha", "beta"]),
    )
    assert exp.name == "Updated Experiment"
    assert exp.tags == ["alpha", "beta"]

    run = await runs_service.create_run(
        RunCreateDTO(
            experiment_id=exp.id,
            project_id=project_id,
            created_by=owner_id,
            name="Run A",
            params={"lr": 0.01},
            git_sha="abc123",
            env="prod",
            notes="first attempt",
            metadata={"batch": 1},
            status=RunStatus.RUNNING,
        )
    )
    assert run.name == "Run A"

    runs, runs_total = await runs_service.list_runs_for_experiment(project_id, exp.id)
    assert len(runs) == 1
    assert runs_total == 1

    session = await sessions_service.create_session(
        CaptureSessionCreateDTO(
            run_id=run.id,
            project_id=project_id,
            ordinal_number=1,
            status=CaptureSessionStatus.RUNNING,
            initiated_by=owner_id,
            notes="Session 1",
        )
    )
    assert session.ordinal_number == 1

    sessions, sessions_total = await sessions_service.list_sessions_for_run(
        project_id, run.id
    )
    assert len(sessions) == 1
    assert sessions_total == 1

    session = await sessions_service.update_session(
        project_id,
        session.id,
        CaptureSessionUpdateDTO(status=CaptureSessionStatus.SUCCEEDED, archived=True),
    )
    assert session.status == CaptureSessionStatus.SUCCEEDED
    assert session.archived is True

    run = await runs_service.update_run(
        project_id,
        run.id,
        RunUpdateDTO(status=RunStatus.SUCCEEDED, duration_seconds=42),
    )
    assert run.status == RunStatus.SUCCEEDED
    assert run.duration_seconds == 42

    await sessions_service.delete_session(project_id, session.id)
    await runs_service.delete_run(project_id, run.id)
    await experiments_service.delete_experiment(project_id, exp.id)

    with pytest.raises(NotFoundError):
        await experiments_service.get_experiment(project_id, exp.id)

    # Invalid transitions
    exp2 = await experiments_service.create_experiment(
        ExperimentCreateDTO(
            project_id=project_id,
            owner_id=owner_id,
            name="Invalid Status Experiment",
        )
    )
    with pytest.raises(InvalidStatusTransitionError):
        await experiments_service.update_experiment(
            project_id,
            exp2.id,
            ExperimentUpdateDTO(status=ExperimentStatus.SUCCEEDED),
        )

    run2 = await runs_service.create_run(
        RunCreateDTO(
            experiment_id=exp2.id,
            project_id=project_id,
            created_by=owner_id,
        )
    )
    with pytest.raises(InvalidStatusTransitionError):
        await runs_service.update_run(
            project_id, run2.id, RunUpdateDTO(status=RunStatus.SUCCEEDED)
        )

    session2 = await sessions_service.create_session(
        CaptureSessionCreateDTO(
            run_id=run2.id,
            project_id=project_id,
            ordinal_number=1,
        )
    )
    with pytest.raises(InvalidStatusTransitionError):
        await sessions_service.update_session(
            project_id,
            session2.id,
            CaptureSessionUpdateDTO(status=CaptureSessionStatus.SUCCEEDED),
        )


@pytest.mark.asyncio
async def test_artifact_repository_is_project_scoped(db_pool):
    """Regression: by-id artifact access must be scoped to the owning project.

    Previously ``ArtifactRepository.get/delete/approve`` looked up rows by
    ``id`` only, allowing a user authorized for project A to read/mutate an
    artifact belonging to project B (cross-project IDOR).
    """
    from experiment_service.repositories.artifacts import ArtifactRepository

    experiments_repo = ExperimentRepository(db_pool)
    runs_repo = RunRepository(db_pool)
    sessions_repo = CaptureSessionRepository(db_pool)
    artifact_repo = ArtifactRepository(db_pool)

    experiments_service = ExperimentService(experiments_repo)
    runs_service = RunService(runs_repo, experiments_repo, sessions_repo)

    project_a = uuid.uuid4()
    project_b = uuid.uuid4()
    owner_id = uuid.uuid4()

    exp = await experiments_service.create_experiment(
        ExperimentCreateDTO(
            project_id=project_a,
            owner_id=owner_id,
            name="Artifact Owner Experiment",
        )
    )
    run = await runs_service.create_run(
        RunCreateDTO(
            experiment_id=exp.id,
            project_id=project_a,
            created_by=owner_id,
        )
    )
    artifact = await artifact_repo.create(
        run_id=run.id,
        project_id=project_a,
        type="model",
        uri="s3://bucket/key",
        created_by=owner_id,
    )

    # Same project — full access.
    assert (await artifact_repo.get(project_a, artifact.id)) is not None

    # Foreign project — must not leak the artifact.
    assert (await artifact_repo.get(project_b, artifact.id)) is None
    assert (await artifact_repo.approve(project_b, artifact.id, owner_id, None)) is None
    assert (await artifact_repo.delete(project_b, artifact.id)) is False

    # The artifact is untouched and still deletable from its own project.
    assert (await artifact_repo.get(project_a, artifact.id)) is not None
    assert (await artifact_repo.delete(project_a, artifact.id)) is True


@pytest.mark.asyncio
async def test_backfill_task_repository_is_project_scoped(db_pool):
    """Regression: backfill task lookups must be scoped to the owning project.

    ``BackfillTaskRepository.get`` / ``list_by_sensor`` previously ignored
    ``project_id``, leaking conversion backfill tasks across projects.
    """
    from experiment_service.repositories.backfill_tasks import BackfillTaskRepository

    repo = BackfillTaskRepository(db_pool)

    project_a = uuid.uuid4()
    project_b = uuid.uuid4()
    sensor_id = uuid.uuid4()
    profile_id = uuid.uuid4()
    owner_id = uuid.uuid4()

    # Minimal FK-valid fixtures: sensor (id, project_id) + conversion profile.
    async with db_pool.acquire() as conn:
        await conn.execute(
            """
            INSERT INTO sensors (id, project_id, name, type, input_unit, display_unit)
            VALUES ($1, $2, 'S', 'temp', 'mV', 'C')
            """,
            sensor_id,
            project_a,
        )
        await conn.execute(
            """
            INSERT INTO conversion_profiles
                (id, sensor_id, project_id, version, kind, created_by)
            VALUES ($1, $2, $3, 'v1', 'linear', $4)
            """,
            profile_id,
            sensor_id,
            project_a,
            owner_id,
        )

    task = await repo.create(
        sensor_id=sensor_id,
        project_id=project_a,
        conversion_profile_id=profile_id,
        created_by=owner_id,
    )

    # Same project — visible.
    assert (await repo.get(project_a, task["id"])) is not None
    items_a, total_a = await repo.list_by_sensor(project_a, sensor_id)
    assert total_a == 1 and len(items_a) == 1

    # Foreign project — must not leak.
    assert (await repo.get(project_b, task["id"])) is None
    items_b, total_b = await repo.list_by_sensor(project_b, sensor_id)
    assert total_b == 0 and items_b == []

