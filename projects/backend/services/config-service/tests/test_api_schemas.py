"""Integration tests: schema management endpoints."""
from __future__ import annotations

import pytest

from tests.utils import ADMIN_HEADERS, VIEWER_HEADERS, make_headers

_SCHEMA_ADMIN_HEADERS = make_headers(
    user_id="schema-admin",
    system_permissions=["configs.schemas.manage"],
)


@pytest.mark.asyncio
async def test_list_schemas(service_client):
    resp = await service_client.get("/api/v1/schemas", headers=ADMIN_HEADERS)
    assert resp.status == 200
    data = await resp.json()
    # Seed data from 002_seed_schemas.sql provides at least feature_flag + qos
    types = [s["config_type"] for s in data["items"]]
    assert "feature_flag" in types
    assert "qos" in types


@pytest.mark.asyncio
async def test_get_schema_feature_flag(service_client):
    resp = await service_client.get(
        "/api/v1/schemas/feature_flag", headers=ADMIN_HEADERS
    )
    assert resp.status == 200
    data = await resp.json()
    assert data["config_type"] == "feature_flag"
    assert data["is_active"] is True
    assert "properties" in data["schema"]


@pytest.mark.asyncio
async def test_get_schema_not_found(service_client):
    resp = await service_client.get(
        "/api/v1/schemas/unknown_type", headers=ADMIN_HEADERS
    )
    assert resp.status == 400  # invalid config_type


@pytest.mark.asyncio
async def test_get_schema_history(service_client):
    resp = await service_client.get(
        "/api/v1/schemas/feature_flag/history", headers=ADMIN_HEADERS
    )
    assert resp.status == 200
    items = (await resp.json())["items"]
    assert len(items) >= 1


@pytest.mark.asyncio
async def test_update_schema_additive(service_client):
    """Adding an optional field is additive → allowed."""
    # Get current schema first
    current = await (
        await service_client.get("/api/v1/schemas/feature_flag", headers=ADMIN_HEADERS)
    ).json()

    new_schema = {
        **current["schema"],
        "properties": {
            **current["schema"]["properties"],
            "rollout_percentage": {"type": "integer", "minimum": 0, "maximum": 100},
        },
    }

    resp = await service_client.put(
        "/api/v1/schemas/feature_flag",
        json={"schema": new_schema},
        headers={**ADMIN_HEADERS, "X-User-System-Permissions": "configs.schemas.manage"},
    )
    assert resp.status == 200, await resp.text()
    data = await resp.json()
    assert data["version"] == current["version"] + 1


@pytest.mark.asyncio
async def test_update_schema_breaking_rejected(service_client):
    """Removing a field is a breaking change → 422."""
    current = await (
        await service_client.get("/api/v1/schemas/feature_flag", headers=ADMIN_HEADERS)
    ).json()

    props = {**current["schema"]["properties"]}
    props.pop("enabled", None)
    breaking_schema = {**current["schema"], "properties": props}

    resp = await service_client.put(
        "/api/v1/schemas/feature_flag",
        json={"schema": breaking_schema},
        headers={**ADMIN_HEADERS, "X-User-System-Permissions": "configs.schemas.manage"},
    )
    assert resp.status == 422


@pytest.mark.asyncio
async def test_update_schema_requires_permission(service_client):
    current = await (
        await service_client.get("/api/v1/schemas/feature_flag", headers=ADMIN_HEADERS)
    ).json()

    resp = await service_client.put(
        "/api/v1/schemas/feature_flag",
        json={"schema": current["schema"]},
        headers=VIEWER_HEADERS,  # no manage permission
    )
    assert resp.status == 403


@pytest.mark.asyncio
async def test_list_schemas_requires_auth(service_client):
    resp = await service_client.get("/api/v1/schemas")
    assert resp.status == 401
