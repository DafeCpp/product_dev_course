"""Telemetry ingest service, conversion backfill (routes, service, worker) on a real database."""
from __future__ import annotations

import uuid
from contextlib import asynccontextmanager
from datetime import UTC, datetime, timedelta

import asyncpg
import pytest

from experiment_service.core.exceptions import (
    NotFoundError,
    ScopeMismatchError,
    UnauthorizedError,
)
from experiment_service.domain.dto import TelemetryIngestDTO, TelemetryReadingDTO
from experiment_service.repositories import (
    CaptureSessionRepository,
    ConversionProfileRepository,
    RunRepository,
    SensorRepository,
    TelemetryRepository,
)
from experiment_service.repositories.backfill_tasks import BackfillTaskRepository
from experiment_service.services.idempotency import IDEMPOTENCY_HEADER
from experiment_service.services.telemetry import TelemetryService
from experiment_service.workers.conversion_backfill import conversion_backfill
from tests.utils import make_headers

T0 = datetime(2026, 1, 1, tzinfo=UTC)


@pytest.fixture
async def db_pool(pgsql):
    pool = await asyncpg.create_pool(dsn=pgsql["experiment_service"].conninfo.get_uri())
    try:
        yield pool
    finally:
        await pool.close()


@pytest.fixture
def telemetry(db_pool) -> TelemetryService:
    return TelemetryService(
        SensorRepository(db_pool),
        RunRepository(db_pool),
        CaptureSessionRepository(db_pool),
        TelemetryRepository(db_pool),
        ConversionProfileRepository(db_pool),
    )


@pytest.fixture
async def ctx(service_client) -> dict:
    """Project with an experiment, a run, a running capture session and a sensor."""
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    experiment = await service_client.post(
        "/api/v1/experiments", json={"project_id": str(project_id), "name": "Telemetry"}, headers=headers
    )
    experiment_id = (await experiment.json())["id"]
    run = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/runs", json={"name": "run"}, headers=headers
    )
    run_id = (await run.json())["id"]
    session = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions",
        json={"ordinal_number": 1, "status": "running"},
        headers=headers,
    )
    session_id = (await session.json())["id"]
    sensor = await service_client.post(
        "/api/v1/sensors",
        json={
            "project_id": str(project_id),
            "name": "thermo",
            "type": "thermocouple",
            "input_unit": "mV",
            "display_unit": "C",
        },
        headers={**headers, IDEMPOTENCY_HEADER: str(uuid.uuid4())},
    )
    body = await sensor.json()
    return {
        "client": service_client,
        "project_id": project_id,
        "headers": headers,
        "run_id": uuid.UUID(run_id),
        "session_id": uuid.UUID(session_id),
        "sensor_id": uuid.UUID(body["sensor"]["id"]),
        "token": body["token"],
    }


async def publish_profile(ctx: dict, *, kind: str = "linear", payload: dict | None = None) -> str:
    payload = payload if payload is not None else {"a": 2.0, "b": 1.0}
    created = await ctx["client"].post(
        f"/api/v1/sensors/{ctx['sensor_id']}/conversion-profiles",
        json={"version": str(uuid.uuid4()), "kind": kind, "payload": payload},
        headers=ctx["headers"],
    )
    assert created.status == 201
    profile_id = (await created.json())["id"]
    published = await ctx["client"].post(
        f"/api/v1/sensors/{ctx['sensor_id']}/conversion-profiles/{profile_id}/publish",
        headers=ctx["headers"],
    )
    assert published.status == 200
    return profile_id


def readings(*raw: float, physical: float | None = None) -> list[TelemetryReadingDTO]:
    return [
        TelemetryReadingDTO(timestamp=T0 + timedelta(seconds=i), raw_value=value, physical_value=physical)
        for i, value in enumerate(raw)
    ]


def payload(ctx: dict, items: list[TelemetryReadingDTO], **extra) -> TelemetryIngestDTO:
    return TelemetryIngestDTO(sensor_id=ctx["sensor_id"], readings=items, **extra)


@asynccontextmanager
async def without_foreign_keys(pool):
    """A transaction that skips FK checks, so tests can plant dangling references without touching the schema."""
    async with pool.acquire() as conn:
        async with conn.transaction():
            await conn.execute("SET LOCAL session_replication_role = replica")
            yield conn


async def stored(pool, sensor_id: uuid.UUID) -> list[asyncpg.Record]:
    return await pool.fetch(
        "SELECT raw_value, physical_value, conversion_status, conversion_profile_id, run_id, capture_session_id "
        "FROM telemetry_records WHERE sensor_id = $1 ORDER BY timestamp",
        sensor_id,
    )


# --- TelemetryService.ingest ----------------------------------------------------------------------


async def test_ingest_without_profile_keeps_raw_values(telemetry, ctx, db_pool):
    count = await telemetry.ingest(payload(ctx, readings(1.0, 2.0)), token=ctx["token"])

    assert count == 2
    rows = await stored(db_pool, ctx["sensor_id"])
    assert [r["conversion_status"] for r in rows] == ["raw_only", "raw_only"]
    assert all(r["physical_value"] is None for r in rows)


async def test_ingest_converts_with_the_active_profile(telemetry, ctx, db_pool):
    profile_id = await publish_profile(ctx)

    await telemetry.ingest(payload(ctx, readings(1.0, 3.0)), token=ctx["token"])

    rows = await stored(db_pool, ctx["sensor_id"])
    assert [r["physical_value"] for r in rows] == [3.0, 7.0]
    assert {r["conversion_status"] for r in rows} == {"converted"}
    assert {str(r["conversion_profile_id"]) for r in rows} == {profile_id}


async def test_ingest_marks_failed_conversions(telemetry, ctx, db_pool):
    profile_id = await publish_profile(ctx)
    await db_pool.execute(
        "UPDATE conversion_profiles SET payload = '{}'::jsonb WHERE id = $1", uuid.UUID(profile_id)
    )

    await telemetry.ingest(payload(ctx, readings(1.0)), token=ctx["token"])

    row = (await stored(db_pool, ctx["sensor_id"]))[0]
    assert row["conversion_status"] == "conversion_failed"
    assert row["physical_value"] is None


async def test_ingest_keeps_client_supplied_physical_values(telemetry, ctx, db_pool):
    profile_id = await publish_profile(ctx)

    await telemetry.ingest(payload(ctx, readings(1.0, physical=42.0)), token=ctx["token"])

    row = (await stored(db_pool, ctx["sensor_id"]))[0]
    assert row["physical_value"] == 42.0
    assert row["conversion_status"] == "client_provided"
    assert str(row["conversion_profile_id"]) == profile_id


async def test_ingest_client_values_without_profile(telemetry, ctx, db_pool):
    await telemetry.ingest(payload(ctx, readings(1.0, physical=5.0)), token=ctx["token"])

    row = (await stored(db_pool, ctx["sensor_id"]))[0]
    assert row["conversion_status"] == "client_provided"
    assert row["conversion_profile_id"] is None


async def test_ingest_survives_a_profile_that_disappeared(telemetry, ctx, db_pool):
    await publish_profile(ctx)
    async with without_foreign_keys(db_pool) as conn:
        await conn.execute(
            "UPDATE sensors SET active_profile_id = $1 WHERE id = $2", uuid.uuid4(), ctx["sensor_id"]
        )

    await telemetry.ingest(payload(ctx, readings(1.0)), token=ctx["token"])

    row = (await stored(db_pool, ctx["sensor_id"]))[0]
    assert row["conversion_status"] == "raw_only"


async def test_ingest_attaches_run_and_session_and_touches_the_sensor(telemetry, ctx, db_pool):
    await telemetry.ingest(
        payload(ctx, readings(1.0, 2.0), run_id=ctx["run_id"], capture_session_id=ctx["session_id"]),
        token=ctx["token"],
    )

    row = (await stored(db_pool, ctx["sensor_id"]))[0]
    assert row["run_id"] == ctx["run_id"]
    assert row["capture_session_id"] == ctx["session_id"]
    heartbeat = await db_pool.fetchval("SELECT last_heartbeat FROM sensors WHERE id = $1", ctx["sensor_id"])
    assert heartbeat == T0 + timedelta(seconds=1)


async def test_ingest_derives_the_run_from_the_capture_session(telemetry, ctx, db_pool):
    await telemetry.ingest(payload(ctx, readings(1.0), capture_session_id=ctx["session_id"]), token=ctx["token"])

    row = (await stored(db_pool, ctx["sensor_id"]))[0]
    assert row["run_id"] == ctx["run_id"]


async def test_ingest_rejects_unknown_credentials(telemetry, ctx):
    with pytest.raises(UnauthorizedError):
        await telemetry.ingest(payload(ctx, readings(1.0)), token="wrong")
    with pytest.raises(UnauthorizedError):
        await telemetry.ingest(
            TelemetryIngestDTO(sensor_id=uuid.uuid4(), readings=readings(1.0)), token=ctx["token"]
        )


async def test_ingest_rejects_a_session_from_another_run(telemetry, ctx):
    client, headers = ctx["client"], ctx["headers"]
    other = await client.post(
        f"/api/v1/experiments/{(await (await client.get('/api/v1/experiments', headers=headers)).json())['experiments'][0]['id']}/runs",
        json={"name": "other"},
        headers=headers,
    )
    other_run = uuid.UUID((await other.json())["id"])

    with pytest.raises(ScopeMismatchError, match="does not belong"):
        await telemetry.ingest(
            payload(ctx, readings(1.0), run_id=other_run, capture_session_id=ctx["session_id"]),
            token=ctx["token"],
        )


async def test_ingest_never_writes_into_archived_scopes(telemetry, ctx, db_pool):
    await db_pool.execute("UPDATE capture_sessions SET archived = true WHERE id = $1", ctx["session_id"])
    with pytest.raises(ScopeMismatchError, match="session is archived"):
        await telemetry.ingest(
            payload(ctx, readings(1.0), capture_session_id=ctx["session_id"]), token=ctx["token"]
        )

    await db_pool.execute("UPDATE capture_sessions SET archived = false WHERE id = $1", ctx["session_id"])
    await db_pool.execute("UPDATE runs SET status = 'archived' WHERE id = $1", ctx["run_id"])
    with pytest.raises(ScopeMismatchError, match="Run is archived"):
        await telemetry.ingest(payload(ctx, readings(1.0), run_id=ctx["run_id"]), token=ctx["token"])


async def test_ingest_reports_unknown_run(telemetry, ctx):
    with pytest.raises(NotFoundError):
        await telemetry.ingest(payload(ctx, readings(1.0), run_id=uuid.uuid4()), token=ctx["token"])


# --- late records ---------------------------------------------------------------------------------------


async def test_late_records_are_attached_to_their_session(db_pool, ctx):
    await db_pool.execute(
        """
        INSERT INTO telemetry_records
            (project_id, sensor_id, timestamp, raw_value, meta, conversion_status)
        VALUES ($1, $2, $3, 1.0, $4::jsonb, 'raw_only')
        """,
        ctx["project_id"],
        ctx["sensor_id"],
        T0,
        '{"__system": {"capture_session_id": "%s", "late": true}}' % ctx["session_id"],
    )

    attached = await TelemetryRepository(db_pool).attach_late_records(ctx["session_id"])

    assert attached == 1
    row = await db_pool.fetchrow("SELECT capture_session_id, meta FROM telemetry_records")
    assert row["capture_session_id"] == ctx["session_id"]
    assert await TelemetryRepository(db_pool).attach_late_records(ctx["session_id"]) == 0
    await TelemetryRepository(db_pool).bulk_insert([])


# --- backfill routes ------------------------------------------------------------------------------------


async def test_backfill_task_lifecycle_through_the_api(ctx):
    client, headers, sensor_id = ctx["client"], ctx["headers"], ctx["sensor_id"]
    base = f"/api/v1/sensors/{sensor_id}/backfill"

    no_profile = await client.post(base, headers=headers)
    assert no_profile.status == 404

    profile_id = await publish_profile(ctx)
    started = await client.post(base, headers=headers)
    assert started.status == 201
    task = await started.json()
    assert task["status"] == "pending"
    assert task["conversion_profile_id"] == profile_id

    listed = await (await client.get(base, headers=headers)).json()
    assert listed["total"] == 1
    assert listed["backfill_tasks"][0]["id"] == task["id"]

    fetched = await client.get(f"{base}/{task['id']}", headers=headers)
    assert (await fetched.json())["id"] == task["id"]
    assert (await client.get(f"{base}/{uuid.uuid4()}", headers=headers)).status == 404


async def test_backfill_routes_enforce_permissions(ctx):
    client, sensor_id = ctx["client"], ctx["sensor_id"]
    viewer = make_headers(ctx["project_id"], role="viewer")

    denied = await client.post(f"/api/v1/sensors/{sensor_id}/backfill", headers=viewer)
    assert denied.status == 403
    allowed = await client.get(f"/api/v1/sensors/{sensor_id}/backfill", headers=viewer)
    assert allowed.status == 200


# --- backfill worker ------------------------------------------------------------------------------------


async def test_worker_has_nothing_to_do_without_tasks(ctx):
    assert await conversion_backfill(T0) is None


async def test_worker_converts_existing_records(telemetry, ctx, db_pool):
    await telemetry.ingest(payload(ctx, readings(1.0, 2.0, 3.0)), token=ctx["token"])
    profile_id = await publish_profile(ctx)
    started = await ctx["client"].post(f"/api/v1/sensors/{ctx['sensor_id']}/backfill", headers=ctx["headers"])
    task_id = (await started.json())["id"]

    result = await conversion_backfill(T0)

    assert result == f"task={task_id} completed: 3 records"
    rows = await stored(db_pool, ctx["sensor_id"])
    assert [r["physical_value"] for r in rows] == [3.0, 5.0, 7.0]
    assert {str(r["conversion_profile_id"]) for r in rows} == {profile_id}
    task = await (
        await ctx["client"].get(f"/api/v1/sensors/{ctx['sensor_id']}/backfill/{task_id}", headers=ctx["headers"])
    ).json()
    assert (task["status"], task["total_records"], task["processed_records"]) == ("completed", 3, 3)
    assert await conversion_backfill(T0) is None


async def test_worker_completes_empty_tasks(ctx):
    await publish_profile(ctx)
    await ctx["client"].post(f"/api/v1/sensors/{ctx['sensor_id']}/backfill", headers=ctx["headers"])

    result = await conversion_backfill(T0)

    assert result is not None and result.endswith("completed: 0 records")


async def test_worker_marks_unconvertible_records(telemetry, ctx, db_pool):
    await telemetry.ingest(payload(ctx, readings(1.0)), token=ctx["token"])
    profile_id = await publish_profile(ctx, kind="polynomial", payload={"coefficients": [1.0, 1.0]})
    await db_pool.execute(
        "UPDATE conversion_profiles SET payload = '{}'::jsonb WHERE id = $1", uuid.UUID(profile_id)
    )
    await ctx["client"].post(f"/api/v1/sensors/{ctx['sensor_id']}/backfill", headers=ctx["headers"])

    await conversion_backfill(T0)

    row = (await stored(db_pool, ctx["sensor_id"]))[0]
    assert row["conversion_status"] == "conversion_failed"


async def test_worker_fails_the_task_when_the_profile_is_gone(ctx, db_pool):
    profile_id = await publish_profile(ctx)
    started = await ctx["client"].post(f"/api/v1/sensors/{ctx['sensor_id']}/backfill", headers=ctx["headers"])
    task_id = (await started.json())["id"]
    async with without_foreign_keys(db_pool) as conn:
        await conn.execute("DELETE FROM conversion_profiles WHERE id = $1", uuid.UUID(profile_id))

    result = await conversion_backfill(T0)

    assert result == f"task={task_id} failed: profile not found"
    task = await (
        await ctx["client"].get(f"/api/v1/sensors/{ctx['sensor_id']}/backfill/{task_id}", headers=ctx["headers"])
    ).json()
    assert task["status"] == "failed"
    assert task["error_message"] == "Conversion profile not found"


async def test_worker_records_unexpected_errors(telemetry, ctx, monkeypatch):
    await telemetry.ingest(payload(ctx, readings(1.0)), token=ctx["token"])
    await publish_profile(ctx)
    started = await ctx["client"].post(f"/api/v1/sensors/{ctx['sensor_id']}/backfill", headers=ctx["headers"])
    task_id = (await started.json())["id"]

    async def explode(self, task_id, total):
        raise RuntimeError("database is on fire")

    monkeypatch.setattr(BackfillTaskRepository, "set_total", explode)

    result = await conversion_backfill(T0)

    assert result == f"task={task_id} failed: database is on fire"
    task = await (
        await ctx["client"].get(f"/api/v1/sensors/{ctx['sensor_id']}/backfill/{task_id}", headers=ctx["headers"])
    ).json()
    assert task["status"] == "failed"
    assert task["error_message"] == "database is on fire"
