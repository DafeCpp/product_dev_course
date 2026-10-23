"""Audit log: every query filter, input validation and failure mapping."""
from __future__ import annotations

import uuid
from datetime import UTC, datetime, timedelta

import pytest

from auth_service.api.routes import audit

ACTOR = str(uuid.uuid4())
SCOPE = str(uuid.uuid4())
URL = "/api/v1/audit-log"
INGEST = "/api/v1/internal/audit"


def bearer(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


async def ingest(client, **overrides) -> dict:
    body = {
        "actor_id": ACTOR,
        "action": "thing.done",
        "scope_type": "project",
        "scope_id": SCOPE,
        "target_type": "widget",
        "target_id": "w-1",
        "details": {"k": "v"},
        "ip_address": "10.1.2.3",
        "user_agent": "pytest",
    }
    body.update(overrides)
    response = await client.post(INGEST, json=body)
    assert response.status == 201
    return await response.json()


@pytest.fixture
async def seeded(service_client, superadmin_token):
    await ingest(service_client)
    await ingest(service_client, action="other.done", actor_id=str(uuid.uuid4()), target_id="w-2")
    return bearer(superadmin_token)


@pytest.mark.parametrize(
    "query,expected",
    [
        (f"actor_id={ACTOR}", 1),
        ("action=thing.done", 1),
        ("action=other.done", 1),
        ("scope_type=project", 2),
        (f"scope_id={SCOPE}", 2),
        ("target_type=widget", 2),
        ("target_id=w-2", 1),
        ("action=nothing.like.this", 0),
    ],
)
async def test_each_filter_narrows_the_log(service_client, seeded, query, expected):
    response = await service_client.get(f"{URL}?{query}", headers=seeded)
    assert response.status == 200
    payload = await response.json()
    assert payload["total"] == expected
    assert len(payload["entries"]) == expected


async def test_date_range_filters(service_client, seeded):
    now = datetime.now(UTC)
    past = (now - timedelta(days=1)).isoformat().replace("+00:00", "Z")
    future = (now + timedelta(days=1)).isoformat().replace("+00:00", "Z")

    inside = await service_client.get(f"{URL}?from={past}&to={future}&scope_type=project", headers=seeded)
    before = await service_client.get(f"{URL}?to={past}&scope_type=project", headers=seeded)
    after = await service_client.get(f"{URL}?from={future}&scope_type=project", headers=seeded)

    assert (await inside.json())["total"] == 2
    assert (await before.json())["total"] == 0
    assert (await after.json())["total"] == 0


@pytest.mark.parametrize(
    "query",
    ["limit=many", "offset=soon", "actor_id=nope", "scope_id=nope", "from=yesterday", "to=tomorrow"],
)
async def test_invalid_query_values_are_rejected(service_client, seeded, query):
    response = await service_client.get(f"{URL}?{query}", headers=seeded)
    assert response.status == 400


async def test_paging_is_clamped(service_client, seeded):
    response = await service_client.get(f"{URL}?limit=100000&offset=-3", headers=seeded)
    payload = await response.json()
    assert payload["limit"] == 500
    assert payload["offset"] == 0


async def test_unexpected_failure_is_reported(service_client, seeded, monkeypatch):
    async def explode(*args, **kwargs):
        raise RuntimeError("database is on fire")

    monkeypatch.setattr(audit, "get_pool", explode)
    response = await service_client.get(URL, headers=seeded)
    assert response.status == 500
    assert (await response.json())["error"] == "database is on fire"


@pytest.mark.parametrize(
    "body,fragment",
    [
        ({"action": "x", "scope_type": "system"}, "Missing fields"),
        ({"actor_id": "nope", "action": "x", "scope_type": "system"}, "Invalid actor_id"),
        (
            {"actor_id": ACTOR, "action": "x", "scope_type": "system", "scope_id": "nope"},
            "Invalid scope_id",
        ),
    ],
)
async def test_ingest_validates_the_payload(service_client, body, fragment):
    response = await service_client.post(INGEST, json=body)
    assert response.status == 400
    assert fragment in await response.text()


async def test_ingest_rejects_non_json(service_client):
    response = await service_client.post(INGEST, data="not json")
    assert response.status == 400


async def test_ingest_stores_every_field(service_client, superadmin_token):
    entry = await ingest(service_client)
    assert entry["actor_id"] == ACTOR
    assert entry["target_type"] == "widget"
    assert entry["details"] == {"k": "v"}
    assert entry["ip_address"] == "10.1.2.3"
    assert entry["user_agent"] == "pytest"
