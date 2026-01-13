from __future__ import annotations

import hashlib
from datetime import datetime, timezone
from uuid import UUID, uuid4

import asyncpg


def _token_hash(token: str) -> bytes:
    return hashlib.sha256(token.encode("utf-8")).digest()


async def _seed(
    *,
    db_uri: str,
    project_id: UUID,
    sensor_id: UUID,
    token: str,
    run_id: UUID,
    capture_session_id: UUID,
) -> None:
    conn = await asyncpg.connect(db_uri)
    try:
        await conn.execute(
            """
            INSERT INTO sensors (id, project_id, status, token_hash)
            VALUES ($1, $2, 'active', $3)
            """,
            sensor_id,
            project_id,
            _token_hash(token),
        )
        await conn.execute(
            """
            INSERT INTO runs (id, project_id)
            VALUES ($1, $2)
            """,
            run_id,
            project_id,
        )
        await conn.execute(
            """
            INSERT INTO capture_sessions (id, run_id, project_id, ordinal_number)
            VALUES ($1, $2, $3, 1)
            """,
            capture_session_id,
            run_id,
            project_id,
        )
    finally:
        await conn.close()


async def test_ingest_happy_path(service_client, pgsql):
    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()
    token = "test-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    ts = datetime(2026, 1, 1, 0, 0, 0, tzinfo=timezone.utc)
    resp = await service_client.post(
        "/api/v1/telemetry",
        json={
            "sensor_id": str(sensor_id),
            "run_id": str(run_id),
            "capture_session_id": str(capture_session_id),
            "meta": {"device_id": "d1"},
            "readings": [
                {"timestamp": ts.isoformat().replace("+00:00", "Z"), "raw_value": 1.23, "meta": {"signal": "a"}}
            ],
        },
        headers={"Authorization": f"Bearer {token}"},
    )
    assert resp.status == 202
    payload = await resp.json()
    assert payload["status"] == "accepted"
    assert payload["accepted"] == 1

    conn = await asyncpg.connect(db_uri)
    try:
        count = await conn.fetchval("SELECT COUNT(*) FROM telemetry_records WHERE sensor_id = $1", sensor_id)
        assert int(count) == 1
        hb = await conn.fetchval("SELECT last_heartbeat FROM sensors WHERE id = $1", sensor_id)
        assert hb is not None
    finally:
        await conn.close()


async def test_ingest_invalid_token_401(service_client, pgsql):
    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()
    token = "valid-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    resp = await service_client.post(
        "/api/v1/telemetry",
        json={"sensor_id": str(sensor_id), "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0}]},
        headers={"Authorization": "Bearer wrong-token"},
    )
    assert resp.status == 401


async def test_ingest_run_capture_mismatch_400(service_client, pgsql):
    project_id = uuid4()
    sensor_id = uuid4()
    token = "t1"

    run_a = uuid4()
    run_b = uuid4()
    capture_b = uuid4()

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    # seed sensor + run_b + capture_b (belongs to run_b) and run_a exists too
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_b,
        capture_session_id=capture_b,
    )
    conn = await asyncpg.connect(db_uri)
    try:
        await conn.execute("INSERT INTO runs (id, project_id) VALUES ($1, $2)", run_a, project_id)
    finally:
        await conn.close()

    resp = await service_client.post(
        "/api/v1/telemetry",
        json={
            "sensor_id": str(sensor_id),
            "run_id": str(run_a),
            "capture_session_id": str(capture_b),
            "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0}],
        },
        headers={"Authorization": f"Bearer {token}"},
    )
    assert resp.status == 400


async def test_ingest_rejects_archived_run_400(service_client, pgsql):
    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()
    token = "test-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    conn = await asyncpg.connect(db_uri)
    try:
        await conn.execute(
            "UPDATE runs SET status = 'archived' WHERE id = $1 AND project_id = $2",
            run_id,
            project_id,
        )
    finally:
        await conn.close()

    resp = await service_client.post(
        "/api/v1/telemetry",
        json={
            "sensor_id": str(sensor_id),
            "run_id": str(run_id),
            "capture_session_id": str(capture_session_id),
            "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0}],
        },
        headers={"Authorization": f"Bearer {token}"},
    )
    assert resp.status == 400


async def test_ingest_rejects_archived_capture_session_400(service_client, pgsql):
    project_id = uuid4()
    sensor_id = uuid4()
    run_id = uuid4()
    capture_session_id = uuid4()
    token = "test-token"

    db_uri = pgsql["telemetry_ingest_service"].conninfo.get_uri()
    await _seed(
        db_uri=db_uri,
        project_id=project_id,
        sensor_id=sensor_id,
        token=token,
        run_id=run_id,
        capture_session_id=capture_session_id,
    )

    conn = await asyncpg.connect(db_uri)
    try:
        await conn.execute(
            "UPDATE capture_sessions SET archived = true WHERE id = $1 AND project_id = $2",
            capture_session_id,
            project_id,
        )
    finally:
        await conn.close()

    resp = await service_client.post(
        "/api/v1/telemetry",
        json={
            "sensor_id": str(sensor_id),
            "run_id": str(run_id),
            "capture_session_id": str(capture_session_id),
            "readings": [{"timestamp": "2026-01-01T00:00:00Z", "raw_value": 1.0}],
        },
        headers={"Authorization": f"Bearer {token}"},
    )
    assert resp.status == 400

