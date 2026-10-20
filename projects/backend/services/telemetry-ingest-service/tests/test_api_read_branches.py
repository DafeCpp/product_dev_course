"""Branch coverage for read endpoints, user-token auth and SSE stream edge cases.

All tests run against the real PostgreSQL/TimescaleDB instance; only the
external auth-service is replaced by a configurable aiohttp stub.
"""

from __future__ import annotations

import hashlib
import json
from datetime import datetime
from typing import Any
from uuid import UUID, uuid4

import asyncpg
import pytest
from aiohttp import web

from telemetry_ingest_service.settings import settings

USER_JWT = "a.b.c"


def _token_hash(token: str) -> bytes:
    return hashlib.sha256(token.encode("utf-8")).digest()


def _db_uri(pgsql) -> str:
    return pgsql["telemetry_ingest_service"].conninfo.get_uri()


async def _seed(db_uri: str, *, token: str = "sensor-token") -> dict[str, UUID]:
    ids = {
        "project_id": uuid4(),
        "sensor_id": uuid4(),
        "run_id": uuid4(),
        "capture_session_id": uuid4(),
    }
    conn = await asyncpg.connect(db_uri)
    try:
        await conn.execute(
            "INSERT INTO sensors (id, project_id, status, token_hash) VALUES ($1, $2, 'active', $3)",
            ids["sensor_id"],
            ids["project_id"],
            _token_hash(token),
        )
        await conn.execute(
            "INSERT INTO runs (id, project_id) VALUES ($1, $2)", ids["run_id"], ids["project_id"]
        )
        await conn.execute(
            """
            INSERT INTO capture_sessions (id, run_id, project_id, ordinal_number, status, archived)
            VALUES ($1, $2, $3, 1, 'running', false)
            """,
            ids["capture_session_id"],
            ids["run_id"],
            ids["project_id"],
        )
    finally:
        await conn.close()
    return ids


async def _insert_record(
    db_uri: str, ids: dict[str, UUID], *, ts: str, raw: float, signal: str = "a"
) -> None:
    conn = await asyncpg.connect(db_uri)
    try:
        await conn.execute(
            """
            INSERT INTO telemetry_records (
                project_id, sensor_id, run_id, capture_session_id,
                timestamp, raw_value, physical_value, meta, conversion_status
            )
            VALUES ($1, $2, $3, $4, $5, $6, $7, $8::jsonb, 'raw_only')
            """,
            ids["project_id"],
            ids["sensor_id"],
            ids["run_id"],
            ids["capture_session_id"],
            datetime.fromisoformat(ts.replace("Z", "+00:00")),
            raw,
            raw * 2,
            json.dumps({"signal": signal}),
        )
    finally:
        await conn.close()


class _AuthBehaviour:
    def __init__(self) -> None:
        self.me_status = 200
        self.me_body: dict[str, Any] = {"id": "user-1"}
        self.members_status = 200
        self.members: list[dict[str, Any]] = [{"user_id": "user-1", "role": "viewer"}]


@pytest.fixture
async def auth_stub(aiohttp_server, monkeypatch):
    behaviour = _AuthBehaviour()
    app = web.Application()

    async def me(_: web.Request) -> web.Response:
        return web.json_response(behaviour.me_body, status=behaviour.me_status)

    async def members(_: web.Request) -> web.Response:
        return web.json_response({"members": behaviour.members}, status=behaviour.members_status)

    app.router.add_get("/auth/me", me)
    app.router.add_get("/projects/{project_id}/members", members)
    server = await aiohttp_server(app)
    # Trailing /api/v1 must be stripped by the authorizer.
    monkeypatch.setattr(settings, "auth_service_url", str(server.make_url("/api/v1")))
    return behaviour


def _auth_headers(token: str = USER_JWT) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


# ---------------------------------------------------------------------------
# User-token authorization outcomes (auth-service responses -> HTTP status)
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("me_status", "me_body", "members_status", "members", "expected"),
    [
        (401, {}, 200, [], 401),
        (200, {}, 200, [], 401),
        (200, {"id": "user-1"}, 403, [], 403),
        (200, {"id": "user-1"}, 404, [], 404),
        (200, {"id": "user-1"}, 500, [], 502),
        (200, {"id": "user-1"}, 200, [{"user_id": "someone-else"}], 403),
    ],
)
async def test_query_maps_auth_service_outcomes(
    service_client,
    pgsql,
    auth_stub,
    me_status,
    me_body,
    members_status,
    members,
    expected,
):
    ids = await _seed(_db_uri(pgsql))
    auth_stub.me_status, auth_stub.me_body = me_status, me_body
    auth_stub.members_status, auth_stub.members = members_status, members

    resp = await service_client.get(
        f"/api/v1/telemetry/query?capture_session_id={ids['capture_session_id']}",
        headers=_auth_headers(),
    )
    assert resp.status == expected


async def test_query_auth_service_unreachable_returns_502(service_client, pgsql, monkeypatch):
    ids = await _seed(_db_uri(pgsql))
    monkeypatch.setattr(settings, "auth_service_url", "http://127.0.0.1:1")
    resp = await service_client.get(
        f"/api/v1/telemetry/query?capture_session_id={ids['capture_session_id']}",
        headers=_auth_headers(),
    )
    assert resp.status == 502


# ---------------------------------------------------------------------------
# /telemetry/query validation
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("query", "expected_text"),
    [
        ("", "capture_session_id is required"),
        ("capture_session_id={cs}&limit=0", "limit must be >= 1"),
        ("capture_session_id={cs}&order=sideways", "Only order=asc or order=desc is supported"),
    ],
)
async def test_query_rejects_invalid_params(service_client, query, expected_text):
    resp = await service_client.get(
        "/api/v1/telemetry/query?" + query.format(cs=uuid4()),
        headers=_auth_headers(),
    )
    assert resp.status == 400
    assert expected_text in await resp.text()


async def test_query_unknown_capture_session_404(service_client, pgsql):
    resp = await service_client.get(
        f"/api/v1/telemetry/query?capture_session_id={uuid4()}",
        headers=_auth_headers(),
    )
    assert resp.status == 404


async def test_query_filters_by_sensor_and_pages_with_next_since_id(
    service_client, pgsql, auth_stub
):
    db_uri = _db_uri(pgsql)
    ids = await _seed(db_uri)
    for i in range(3):
        await _insert_record(db_uri, ids, ts=f"2026-01-01T00:00:0{i}Z", raw=float(i))

    resp = await service_client.get(
        f"/api/v1/telemetry/query?capture_session_id={ids['capture_session_id']}"
        f"&sensor_id={ids['sensor_id']}&limit=2",
        headers=_auth_headers(),
    )
    assert resp.status == 200
    body = await resp.json()
    assert [p["raw_value"] for p in body["points"]] == [0.0, 1.0]
    assert body["next_since_id"] == body["points"][-1]["id"]

    resp = await service_client.get(
        f"/api/v1/telemetry/query?capture_session_id={ids['capture_session_id']}"
        f"&sensor_id={ids['sensor_id']}&since_id={body['next_since_id']}&limit=2",
        headers=_auth_headers(),
    )
    body = await resp.json()
    assert [p["raw_value"] for p in body["points"]] == [2.0]
    assert body["next_since_id"] is None


# ---------------------------------------------------------------------------
# /telemetry/aggregated
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("query", "expected_text"),
    [
        ("", "capture_session_id is required"),
        ("capture_session_id={cs}&limit=0", "limit must be >= 1"),
        ("capture_session_id={cs}&order=up", "Only order=asc or order=desc is supported"),
    ],
)
async def test_aggregated_rejects_invalid_params(service_client, query, expected_text):
    resp = await service_client.get(
        "/api/v1/telemetry/aggregated?" + query.format(cs=uuid4()),
        headers=_auth_headers(),
    )
    assert resp.status == 400
    assert expected_text in await resp.text()


async def test_aggregated_requires_user_token(service_client):
    resp = await service_client.get(
        f"/api/v1/telemetry/aggregated?capture_session_id={uuid4()}",
        headers={"Authorization": "Bearer sensor-token"},
    )
    assert resp.status == 401


async def test_aggregated_unknown_capture_session_404(service_client, pgsql):
    resp = await service_client.get(
        f"/api/v1/telemetry/aggregated?capture_session_id={uuid4()}",
        headers=_auth_headers(),
    )
    assert resp.status == 404


async def test_aggregated_returns_buckets_with_filters(service_client, pgsql, auth_stub):
    db_uri = _db_uri(pgsql)
    ids = await _seed(db_uri)
    await _insert_record(db_uri, ids, ts="2026-01-01T00:00:10Z", raw=1.0, signal="a")
    await _insert_record(db_uri, ids, ts="2026-01-01T00:00:20Z", raw=3.0, signal="a")
    await _insert_record(db_uri, ids, ts="2026-01-01T00:01:10Z", raw=5.0, signal="a")
    await _insert_record(db_uri, ids, ts="2026-01-01T00:00:30Z", raw=9.0, signal="b")
    conn = await asyncpg.connect(db_uri)
    try:
        await conn.execute("REFRESH MATERIALIZED VIEW telemetry_1m")
    finally:
        await conn.close()

    base = (
        f"/api/v1/telemetry/aggregated?capture_session_id={ids['capture_session_id']}"
        f"&sensor_id={ids['sensor_id']}&signal=a"
    )
    resp = await service_client.get(base, headers=_auth_headers())
    assert resp.status == 200
    body = await resp.json()
    assert body["bucket_interval"] == "1m"
    buckets = body["buckets"]
    assert [b["bucket"] for b in buckets] == ["2026-01-01T00:00:00Z", "2026-01-01T00:01:00Z"]
    assert buckets[0]["sample_count"] == 2
    assert buckets[0]["avg_raw"] == 2.0
    assert buckets[0]["max_physical"] == 6.0
    assert buckets[0]["signal"] == "a"
    assert buckets[0]["sensor_id"] == str(ids["sensor_id"])

    resp = await service_client.get(
        base + "&time_from=2026-01-01T00:01:00Z&time_to=2026-01-01T00:05:00Z",
        headers=_auth_headers(),
    )
    assert [b["bucket"] for b in (await resp.json())["buckets"]] == ["2026-01-01T00:01:00Z"]

    resp = await service_client.get(base + "&order=desc&limit=1", headers=_auth_headers())
    assert [b["bucket"] for b in (await resp.json())["buckets"]] == ["2026-01-01T00:01:00Z"]


# ---------------------------------------------------------------------------
# /sensors/{id}/error-log validation
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("query", "expected_text"),
    [("limit=0", "limit must be >= 1"), ("offset=-1", "offset must be >= 0")],
)
async def test_error_log_rejects_invalid_paging(service_client, query, expected_text):
    resp = await service_client.get(
        f"/api/v1/sensors/{uuid4()}/error-log?{query}", headers=_auth_headers()
    )
    assert resp.status == 400
    assert expected_text in await resp.text()


async def test_error_log_unknown_sensor_404(service_client, pgsql):
    resp = await service_client.get(f"/api/v1/sensors/{uuid4()}/error-log", headers=_auth_headers())
    assert resp.status == 404


# ---------------------------------------------------------------------------
# /telemetry/stream edge cases
# ---------------------------------------------------------------------------


async def test_stream_requires_sensor_id(service_client):
    resp = await service_client.get(
        "/api/v1/telemetry/stream", headers=_auth_headers("sensor-token")
    )
    assert resp.status == 400


async def test_stream_invalid_sensor_token_401(service_client, pgsql):
    ids = await _seed(_db_uri(pgsql))
    resp = await service_client.get(
        f"/api/v1/telemetry/stream?sensor_id={ids['sensor_id']}",
        headers=_auth_headers("wrong-token"),
    )
    assert resp.status == 401


async def test_stream_user_token_unknown_sensor_404(service_client, pgsql):
    resp = await service_client.get(
        f"/api/v1/telemetry/stream?sensor_id={uuid4()}", headers=_auth_headers()
    )
    assert resp.status == 404


async def test_stream_user_token_forbidden_for_non_member(service_client, pgsql, auth_stub):
    ids = await _seed(_db_uri(pgsql))
    auth_stub.members = []
    resp = await service_client.get(
        f"/api/v1/telemetry/stream?sensor_id={ids['sensor_id']}", headers=_auth_headers()
    )
    assert resp.status == 403


async def test_stream_sends_heartbeat_and_closes_on_idle_timeout(
    service_client, pgsql, monkeypatch
):
    ids = await _seed(_db_uri(pgsql))
    monkeypatch.setattr(settings, "telemetry_stream_heartbeat_seconds", 0.0)
    monkeypatch.setattr(settings, "telemetry_stream_poll_interval_seconds", 0.01)
    resp = await service_client.get(
        f"/api/v1/telemetry/stream?sensor_id={ids['sensor_id']}&idle_timeout_seconds=0.05",
        headers=_auth_headers("sensor-token"),
    )
    assert resp.status == 200
    body = await resp.read()
    assert b": heartbeat" in body
    assert b"event: telemetry" not in body


async def test_stream_since_ts_skips_older_records(service_client, pgsql, monkeypatch):
    db_uri = _db_uri(pgsql)
    ids = await _seed(db_uri)
    await _insert_record(db_uri, ids, ts="2026-01-01T00:00:00Z", raw=1.0)
    await _insert_record(db_uri, ids, ts="2026-01-02T00:00:00Z", raw=2.0)
    monkeypatch.setattr(settings, "telemetry_stream_poll_interval_seconds", 0.01)
    resp = await service_client.get(
        f"/api/v1/telemetry/stream?sensor_id={ids['sensor_id']}&since_ts=2026-01-01T12:00:00Z"
        "&idle_timeout_seconds=0.1",
        headers=_auth_headers("sensor-token"),
    )
    body = await resp.read()
    assert body.count(b"event: telemetry") == 1
    assert b'"raw_value":2.0' in body


async def test_stream_reports_error_event_when_read_fails(service_client, pgsql, monkeypatch):
    from telemetry_ingest_service.services.telemetry_read import TelemetryReadService

    ids = await _seed(_db_uri(pgsql))

    async def boom(*_: object, **__: object) -> list[dict[str, Any]]:
        raise RuntimeError("db exploded")

    monkeypatch.setattr(TelemetryReadService, "stream_records", boom)
    resp = await service_client.get(
        f"/api/v1/telemetry/stream?sensor_id={ids['sensor_id']}",
        headers=_auth_headers("sensor-token"),
    )
    assert resp.status == 200
    assert b"event: error\ndata: stream error" in await resp.read()
