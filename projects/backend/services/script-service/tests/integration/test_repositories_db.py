"""Repository and health tests against a real PostgreSQL database."""
from __future__ import annotations

from datetime import UTC, datetime
from unittest.mock import AsyncMock, patch
from uuid import UUID, uuid4

import asyncpg  # type: ignore[import-untyped]
import pytest

from script_service.domain.models import ExecutionStatus, ScriptType
from script_service.repositories.executions import ExecutionRepository
from script_service.repositories.scripts import ScriptRepository

_USER_ID = UUID("550e8400-e29b-41d4-a716-446655440001")


@pytest.fixture
async def pool(database_url):
    pool = await asyncpg.create_pool(dsn=database_url, min_size=1, max_size=2)
    yield pool
    await pool.close()


@pytest.fixture
async def script_repo(pool) -> ScriptRepository:
    return ScriptRepository(pool)


@pytest.fixture
async def execution_repo(pool) -> ExecutionRepository:
    return ExecutionRepository(pool)


async def _create_script(repo: ScriptRepository, name: str, **overrides):
    values = dict(
        name=name,
        description="d",
        target_service="experiment-service",
        script_type=ScriptType.python,
        script_body="print(1)",
        parameters_schema={"type": "object"},
        timeout_sec=30,
        created_by=_USER_ID,
    )
    values.update(overrides)
    return await repo.create(**values)


# --- scripts -----------------------------------------------------------------------------------------------


async def test_script_get_by_name(script_repo):
    created = await _create_script(script_repo, "by-name")

    found = await script_repo.get_by_name("by-name")
    assert found is not None
    assert found.id == created.id
    assert found.parameters_schema == {"type": "object"}

    assert await script_repo.get_by_name("missing") is None
    assert await script_repo.get_by_id(uuid4()) is None


async def test_script_update_variants(script_repo):
    created = await _create_script(script_repo, "to-update")

    # No fields: returns the current record untouched.
    same = await script_repo.update(created.id)
    assert same is not None and same.description == "d"

    updated = await script_repo.update(
        created.id,
        description="new",
        script_type=ScriptType.bash,
        parameters_schema={"a": 1},
        timeout_sec=99,
    )
    assert updated is not None
    assert updated.description == "new"
    assert updated.script_type == ScriptType.bash
    assert updated.parameters_schema == {"a": 1}
    assert updated.timeout_sec == 99

    assert await script_repo.update(uuid4(), description="x") is None


async def test_script_list_filters_and_paging(script_repo):
    await _create_script(script_repo, "s-a", target_service="svc-a")
    second = await _create_script(script_repo, "s-b", target_service="svc-b")
    await script_repo.soft_delete(second.id)

    assert {s.name for s in await script_repo.list(target_service="svc-a")} == {"s-a"}
    assert {s.name for s in await script_repo.list(is_active=False)} == {"s-b"}
    assert {s.name for s in await script_repo.list(is_active=True)} == {"s-a"}
    assert len(await script_repo.list(limit=1)) == 1
    assert await script_repo.list(limit=1, offset=5) == []


async def test_script_soft_delete_missing(script_repo):
    assert await script_repo.soft_delete(uuid4()) is False


async def test_script_name_is_unique(script_repo):
    await _create_script(script_repo, "dup")
    with pytest.raises(asyncpg.UniqueViolationError):
        await _create_script(script_repo, "dup")


# --- executions --------------------------------------------------------------------------------------------


async def test_execution_create_and_get(script_repo, execution_repo):
    script = await _create_script(script_repo, "exec-script")
    created = await execution_repo.create(
        script_id=script.id, parameters={"k": "v"}, requested_by=_USER_ID, target_instance="i-1"
    )

    assert created.status == ExecutionStatus.pending
    assert created.parameters == {"k": "v"}
    assert created.target_instance == "i-1"

    fetched = await execution_repo.get_by_id(created.id)
    assert fetched is not None and fetched.id == created.id
    assert await execution_repo.get_by_id(uuid4()) is None


async def test_execution_update_status_sets_every_field(script_repo, execution_repo):
    script = await _create_script(script_repo, "exec-update")
    execution = await execution_repo.create(script.id, {}, _USER_ID)
    started = datetime(2025, 1, 1, tzinfo=UTC)
    finished = datetime(2025, 1, 2, tzinfo=UTC)

    updated = await execution_repo.update_status(
        execution.id,
        ExecutionStatus.failed,
        started_at=started,
        finished_at=finished,
        exit_code=2,
        stdout="out",
        stderr="err",
        error_message="boom",
    )

    assert updated is not None
    assert updated.status == ExecutionStatus.failed
    assert (updated.started_at, updated.finished_at) == (started, finished)
    assert (updated.exit_code, updated.stdout, updated.stderr) == (2, "out", "err")
    assert updated.error_message == "boom"


async def test_execution_update_status_only_status(script_repo, execution_repo):
    script = await _create_script(script_repo, "exec-status-only")
    execution = await execution_repo.create(script.id, {}, _USER_ID)

    updated = await execution_repo.update_status(execution.id, ExecutionStatus.running)
    assert updated is not None
    assert updated.status == ExecutionStatus.running
    assert updated.exit_code is None and updated.stdout is None

    assert await execution_repo.update_status(uuid4(), ExecutionStatus.running) is None


async def test_execution_list_filters(script_repo, execution_repo):
    first = await _create_script(script_repo, "list-1")
    second = await _create_script(script_repo, "list-2")
    other_user = uuid4()
    run_a = await execution_repo.create(first.id, {}, _USER_ID)
    run_b = await execution_repo.create(second.id, {}, other_user)
    await execution_repo.update_status(run_b.id, ExecutionStatus.completed)

    assert {e.id for e in await execution_repo.list(script_id=first.id)} == {run_a.id}
    assert {e.id for e in await execution_repo.list(status=ExecutionStatus.completed)} == {run_b.id}
    assert {e.id for e in await execution_repo.list(requested_by=other_user)} == {run_b.id}
    assert len(await execution_repo.list()) == 2
    assert len(await execution_repo.list(limit=1)) == 1
    assert await execution_repo.list(offset=10) == []


# --- health ------------------------------------------------------------------------------------------------


async def test_health_reports_ok(service_client):
    response = await service_client.get("/health")
    assert response.status == 200
    body = await response.json()
    assert body["status"] == "ok"
    assert body["db"] == {"status": "ok"}
    assert body["uptime_seconds"] >= 0


async def test_health_degraded_when_database_unreachable(service_client):
    broken = AsyncMock(side_effect=RuntimeError("db down"))
    with patch("script_service.api.routes.health.get_pool", broken):
        response = await service_client.get("/health")

    assert response.status == 503
    body = await response.json()
    assert body["status"] == "degraded"
    assert body["db"] == {"status": "error", "error": "db down"}
