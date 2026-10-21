"""Run the load generator against the real app and test database.

Covers the whole loop the README describes: provision sensors in the DB,
drive REST and WS ingest, classify 202/429/401 outcomes, then clean up.
"""
from __future__ import annotations

from uuid import uuid4

import asyncpg

from ingest_loadtest.provision import cleanup_sensors, provision_sensors
from ingest_loadtest.runner import SensorCredential, run_scenario
from ingest_loadtest.scenarios import Scenario, SlaThresholds
from telemetry_ingest_service.middleware.rate_limit_config import RateLimitConfig
from telemetry_ingest_service.middleware.rest_rate_limit import IngestRateLimiter
from telemetry_ingest_service.middleware.ws_rate_limit import WsRateLimiter

_SLA = SlaThresholds(max_error_rate=0.0, max_rate_limited_rate=0.0)


def _db_uri(pgsql) -> str:
    return pgsql["telemetry_ingest_service"].conninfo.get_uri()


def _base_url(service_client) -> str:
    return str(service_client.make_url("")).rstrip("/")


async def _stored_readings(db_uri: str, sensor_ids: list[str]) -> int:
    conn = await asyncpg.connect(db_uri)
    try:
        return await conn.fetchval(
            "SELECT count(*) FROM telemetry_records WHERE sensor_id = ANY($1::uuid[])",
            sensor_ids,
        )
    finally:
        await conn.close()


async def test_rest_scenario_ingests_every_reading(service_client, pgsql) -> None:
    db_uri = _db_uri(pgsql)
    creds = await provision_sensors(db_uri, 2)
    sc = Scenario(name="it-rest", sensors=2, rate=20.0, batch_size=5, duration_s=0.25, sla=_SLA)

    report = await run_scenario(sc, creds, _base_url(service_client))

    assert report.passed, report.violations
    assert report.accepted_batches == 2 * 5
    assert report.accepted_readings == 2 * 5 * 5
    assert report.latency_ms["p95"] is not None
    assert await _stored_readings(db_uri, [c.sensor_id for c in creds]) == 50


async def test_ws_scenario_ingests_every_reading(service_client, pgsql) -> None:
    db_uri = _db_uri(pgsql)
    creds = await provision_sensors(db_uri, 2)
    sc = Scenario(
        name="it-ws", transport="ws", sensors=2, rate=20.0, batch_size=4, duration_s=0.25, sla=_SLA
    )

    report = await run_scenario(sc, creds, _base_url(service_client))

    assert report.passed, report.violations
    assert report.accepted_readings == 2 * 5 * 4
    assert await _stored_readings(db_uri, [c.sensor_id for c in creds]) == 40


async def test_unpaced_scenario_runs_until_duration(service_client, pgsql) -> None:
    creds = await provision_sensors(_db_uri(pgsql), 1)
    sc = Scenario(name="it-ceiling", rate=0.0, batch_size=2, duration_s=0.3, sla=_SLA)

    report = await run_scenario(sc, creds, _base_url(service_client))

    assert report.passed, report.violations
    assert report.accepted_batches > 1
    assert report.elapsed_s >= 0.3


def _tight_config() -> RateLimitConfig:
    return RateLimitConfig(
        rest_max_requests=2,
        rest_max_readings=0,
        rest_window_seconds=60.0,
        ws_max_messages=2,
        ws_max_readings=0,
        ws_window_seconds=60.0,
        spool_flush_timeout_seconds=5.0,
        ws_max_message_bytes=1048576,
    )


async def test_rest_429_counts_as_rate_limited(service_client, pgsql, monkeypatch) -> None:
    monkeypatch.setattr(
        "telemetry_ingest_service.api.routes.telemetry._rest_limiter",
        IngestRateLimiter(_tight_config()),
    )
    creds = await provision_sensors(_db_uri(pgsql), 1)
    sc = Scenario(name="it-burst", burst_size=5, burst_every_s=1.0, duration_s=1.0, sla=_SLA)

    report = await run_scenario(sc, creds, _base_url(service_client))

    assert report.accepted_batches == 2
    assert report.rate_limited == 3
    assert report.errors == {}
    assert any("rate-limited" in v for v in report.violations)


async def test_ws_rate_limited_frame_counts_as_rate_limited(
    service_client, pgsql, monkeypatch
) -> None:
    monkeypatch.setattr(
        "telemetry_ingest_service.api.routes.ws_ingest._ws_limiter",
        WsRateLimiter(_tight_config()),
    )
    creds = await provision_sensors(_db_uri(pgsql), 1)
    sc = Scenario(
        name="it-ws-burst", transport="ws", burst_size=4, burst_every_s=1.0, duration_s=1.0
    )

    report = await run_scenario(sc, creds, _base_url(service_client))

    assert (report.accepted_batches, report.rate_limited, report.errors) == (2, 2, {})


async def test_bad_token_is_an_error(service_client, pgsql) -> None:
    creds = [SensorCredential(sensor_id=str(uuid4()), token="nope")]
    rest = Scenario(name="it-401", rate=10.0, duration_s=0.2, sla=_SLA)
    ws = Scenario(name="it-ws-401", transport="ws", rate=10.0, duration_s=0.2, sla=_SLA)

    rest_report = await run_scenario(rest, creds, _base_url(service_client))
    ws_report = await run_scenario(ws, creds, _base_url(service_client))

    assert rest_report.errors == {"http_401": 2}
    assert ws_report.errors == {"ws_handshake_401": 1}
    assert not rest_report.passed and not ws_report.passed


async def test_unreachable_service_is_an_error(pgsql) -> None:
    creds = [SensorCredential(sensor_id=str(uuid4()), token="t")]
    sc = Scenario(name="it-down", rate=10.0, duration_s=0.1)

    report = await run_scenario(sc, creds, "http://127.0.0.1:9", request_timeout_s=1.0)

    assert report.accepted_batches == 0
    assert sum(report.errors.values()) == 1
    assert next(iter(report.errors)).startswith("Client")


async def test_cleanup_removes_only_loadtest_sensors(service_client, pgsql) -> None:
    db_uri = _db_uri(pgsql)
    creds = await provision_sensors(db_uri, 3)
    other = uuid4()
    conn = await asyncpg.connect(db_uri)
    try:
        await conn.execute(
            "INSERT INTO sensors (id, project_id, name, status) VALUES ($1, $2, 'loadtest-real', 'active')",
            other,
            uuid4(),
        )
    finally:
        await conn.close()
    sc = Scenario(name="it-clean", sensors=3, rate=10.0, batch_size=2, duration_s=0.1)
    await run_scenario(sc, creds, _base_url(service_client))

    assert await cleanup_sensors(db_uri) == 3

    conn = await asyncpg.connect(db_uri)
    try:
        assert await conn.fetchval("SELECT count(*) FROM sensors") == 1
        assert await conn.fetchval("SELECT id FROM sensors") == other
    finally:
        await conn.close()
