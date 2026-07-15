"""Integration tests: Idempotency-Key header."""

from __future__ import annotations

import uuid

import pytest

from tests.utils import ADMIN_HEADERS, make_headers

_PAYLOAD = {
    "service_name": "idem-svc",
    "key": "idem_flag",
    "config_type": "feature_flag",
    "value": {"enabled": True},
}


@pytest.mark.asyncio
async def test_same_key_returns_same_id(service_client):
    idem_key = str(uuid.uuid4())
    hdrs = {**ADMIN_HEADERS, "Idempotency-Key": idem_key}

    r1 = await service_client.post("/api/v1/config", json=_PAYLOAD, headers=hdrs)
    assert r1.status == 201, await r1.text()
    id1 = (await r1.json())["id"]

    r2 = await service_client.post("/api/v1/config", json=_PAYLOAD, headers=hdrs)
    assert r2.status == 201
    id2 = (await r2.json())["id"]

    assert id1 == id2


@pytest.mark.asyncio
async def test_different_body_same_key_returns_409(service_client):
    idem_key = str(uuid.uuid4())
    hdrs = {**ADMIN_HEADERS, "Idempotency-Key": idem_key}

    r1 = await service_client.post("/api/v1/config", json=_PAYLOAD, headers=hdrs)
    assert r1.status == 201

    different_payload = {**_PAYLOAD, "value": {"enabled": False}}
    r2 = await service_client.post("/api/v1/config", json=different_payload, headers=hdrs)
    assert r2.status == 409


@pytest.mark.asyncio
async def test_different_user_same_key_creates_new(service_client):
    """Same Idempotency-Key from different users: each user's own request is idempotent,
    but different users with the same key don't conflict with each other."""
    idem_key = str(uuid.uuid4())
    suffix = uuid.uuid4().hex[:6]
    # Each user creates a DIFFERENT config key to avoid the unique (service,key) constraint
    payload_a = {**_PAYLOAD, "key": f"idem_user_a_{suffix}"}
    payload_b = {**_PAYLOAD, "key": f"idem_user_b_{suffix}"}

    hdrs_a = {**ADMIN_HEADERS, "Idempotency-Key": idem_key}
    hdrs_b = {
        **make_headers(user_id="other-admin", is_superadmin=True),
        "Idempotency-Key": idem_key,
    }

    r1 = await service_client.post("/api/v1/config", json=payload_a, headers=hdrs_a)
    assert r1.status == 201
    id1 = (await r1.json())["id"]

    r2 = await service_client.post("/api/v1/config", json=payload_b, headers=hdrs_b)
    assert r2.status == 201
    id2 = (await r2.json())["id"]

    assert id1 != id2

    # User A repeating with same key and payload returns same id (idempotent)
    r3 = await service_client.post("/api/v1/config", json=payload_a, headers=hdrs_a)
    assert r3.status == 201
    assert (await r3.json())["id"] == id1


@pytest.mark.asyncio
async def test_no_idempotency_key_creates_duplicates(service_client):
    """Without Idempotency-Key, two POSTs with different keys work fine."""
    p1 = {**_PAYLOAD, "key": f"no_idem_a_{uuid.uuid4().hex[:6]}"}
    p2 = {**_PAYLOAD, "key": f"no_idem_b_{uuid.uuid4().hex[:6]}"}

    r1 = await service_client.post("/api/v1/config", json=p1, headers=ADMIN_HEADERS)
    r2 = await service_client.post("/api/v1/config", json=p2, headers=ADMIN_HEADERS)

    assert r1.status == 201
    assert r2.status == 201
    assert (await r1.json())["id"] != (await r2.json())["id"]


@pytest.mark.asyncio
async def test_sensitive_replay_reapplies_current_permissions_without_second_write(service_client):
    idem_key = str(uuid.uuid4())
    payload = {**_PAYLOAD, "key": "idem_permission_change", "is_sensitive": True}
    reader = make_headers(
        user_id="idem-changing-user",
        system_permissions=["configs.create", "configs.view", "configs.sensitive.read"],
    )
    revoked = make_headers(
        user_id="idem-changing-user",
        system_permissions=["configs.create", "configs.view"],
    )

    first = await service_client.post(
        "/api/v1/config", json=payload, headers={**reader, "Idempotency-Key": idem_key}
    )
    assert first.status == 201, await first.text()
    assert (await first.json())["value"] == {"enabled": True}

    replay = await service_client.post(
        "/api/v1/config", json=payload, headers={**revoked, "Idempotency-Key": idem_key}
    )
    assert replay.status == 201, await replay.text()
    assert (await replay.json())["value"] == "***"

    listed = await service_client.get("/api/v1/config?service=idem-svc", headers=revoked)
    matching = [item for item in (await listed.json())["items"] if item["key"] == payload["key"]]
    assert len(matching) == 1


@pytest.mark.asyncio
async def test_sensitive_replay_reveals_cached_full_value_after_permission_grant(service_client):
    idem_key = str(uuid.uuid4())
    payload = {**_PAYLOAD, "key": "idem_permission_granted", "is_sensitive": True}
    creator = make_headers(user_id="idem-granted-user", system_permissions=["configs.create"])
    reader = make_headers(
        user_id="idem-granted-user",
        system_permissions=["configs.create", "configs.sensitive.read"],
    )

    first = await service_client.post(
        "/api/v1/config", json=payload, headers={**creator, "Idempotency-Key": idem_key}
    )
    assert first.status == 201, await first.text()
    assert (await first.json())["value"] == "***"

    replay = await service_client.post(
        "/api/v1/config", json=payload, headers={**reader, "Idempotency-Key": idem_key}
    )
    assert replay.status == 201, await replay.text()
    assert (await replay.json())["value"] == {"enabled": True}
