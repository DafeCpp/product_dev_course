"""Create and remove load-test sensors directly in experiment_db.

The ingest service authenticates a sensor by ``sensors.token_hash`` alone,
so writing rows straight into the database is enough to get working
credentials without the auth-service / experiment-service API. Meant for
local and staging stacks only.
"""
from __future__ import annotations

import hashlib
import json
import secrets
from pathlib import Path
from uuid import UUID, uuid4

import asyncpg

from ingest_loadtest.runner import SensorCredential

NAME_PREFIX = "loadtest-"
SENSOR_TYPE = "loadtest"


def _hash_token(token: str) -> bytes:
    # Same as telemetry_ingest_service.services.telemetry.hash_sensor_token.
    return hashlib.sha256(token.encode("utf-8")).digest()


async def provision_sensors(
    database_url: str, count: int, project_id: UUID | None = None
) -> list[SensorCredential]:
    if count < 1:
        raise ValueError("count must be >= 1")
    project = project_id or uuid4()
    tag = secrets.token_hex(4)
    creds: list[SensorCredential] = []
    rows = []
    for i in range(count):
        sensor_id = uuid4()
        token = secrets.token_urlsafe(32)
        rows.append(
            (sensor_id, project, f"{NAME_PREFIX}{tag}-{i}", _hash_token(token), token[:4])
        )
        creds.append(SensorCredential(sensor_id=str(sensor_id), token=token))

    conn = await asyncpg.connect(database_url)
    try:
        await conn.executemany(
            """
            INSERT INTO sensors
                (id, project_id, name, type, input_unit, display_unit, status,
                 token_hash, token_preview)
            VALUES ($1, $2, $3, $4, 'raw', 'raw', 'active', $5, $6)
            """,
            [(sid, pid, name, SENSOR_TYPE, th, prev) for sid, pid, name, th, prev in rows],
        )
    finally:
        await conn.close()
    return creds


async def cleanup_sensors(database_url: str) -> int:
    """Delete every load-test sensor; their telemetry goes with them (ON DELETE CASCADE)."""
    conn = await asyncpg.connect(database_url)
    try:
        result = await conn.execute(
            "DELETE FROM sensors WHERE type = $1 AND name LIKE $2",
            SENSOR_TYPE,
            f"{NAME_PREFIX}%",
        )
    finally:
        await conn.close()
    return int(result.split()[-1])


def save_credentials(path: Path, creds: list[SensorCredential]) -> None:
    payload = [{"sensor_id": c.sensor_id, "token": c.token} for c in creds]
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    path.chmod(0o600)


def load_credentials(path: Path) -> list[SensorCredential]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, list):
        raise ValueError(f"{path}: expected a JSON list of {{sensor_id, token}} objects")
    creds = []
    for i, item in enumerate(data):
        try:
            creds.append(SensorCredential(sensor_id=str(item["sensor_id"]), token=str(item["token"])))
        except (KeyError, TypeError) as exc:
            raise ValueError(f"{path}: entry {i} needs 'sensor_id' and 'token'") from exc
    return creds
