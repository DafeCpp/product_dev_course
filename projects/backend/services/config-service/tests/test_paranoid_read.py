"""Integration tests: paranoid read — invalid DB values don't crash the API."""
from __future__ import annotations

import asyncio

import asyncpg
import pytest

from tests.utils import ADMIN_HEADERS


@pytest.mark.asyncio
async def test_paranoid_read_invalid_value_passes_through(service_client, pgsql):
    """Manually corrupt a config value in DB; GET should return it as-is (not 500)."""
    # Create a valid config
    create_resp = await service_client.post(
        "/api/v1/config",
        json={
            "service_name": "paranoid-svc",
            "key": "paranoid_flag",
            "config_type": "feature_flag",
            "value": {"enabled": True},
        },
        headers=ADMIN_HEADERS,
    )
    assert create_resp.status == 201
    config_id = (await create_resp.json())["id"]

    # Directly corrupt the value in the database
    conninfo = pgsql["config_service"].conninfo
    conn = await asyncpg.connect(conninfo.get_uri())
    try:
        await conn.execute(
            "UPDATE configs SET value = $1::jsonb WHERE id = $2::uuid",
            '{"enabled": "not-a-boolean"}',
            config_id,
        )
    finally:
        await conn.close()

    # GET should succeed (paranoid mode: return as-is, don't raise)
    get_resp = await service_client.get(
        f"/api/v1/config/{config_id}", headers=ADMIN_HEADERS
    )
    assert get_resp.status == 200
    data = await get_resp.json()
    assert data["value"] == {"enabled": "not-a-boolean"}
