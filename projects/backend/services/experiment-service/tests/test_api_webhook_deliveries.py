from __future__ import annotations

import uuid

import asyncpg
import pytest

from tests.utils import make_headers


@pytest.mark.asyncio
async def test_webhook_deliveries_list_and_retry(service_client, pgsql):
    project_id = uuid.uuid4()
    headers_owner = make_headers(project_id, role="owner")
    headers_viewer = make_headers(project_id, role="viewer")

    # Create subscription
    resp = await service_client.post(
        "/api/v1/webhooks",
        json={
            "target_url": "http://example.com/hook",
            "event_types": ["run.started"],
            "secret": "s",
        },
        headers=headers_owner,
    )
    assert resp.status == 201
    sub_id = (await resp.json())["id"]

    # Insert a failed delivery manually
    conninfo = pgsql["experiment_service"].conninfo
    conn = await asyncpg.connect(dsn=conninfo.get_uri())
    try:
        delivery_id = await conn.fetchval(
            """
            INSERT INTO webhook_deliveries (
                subscription_id,
                project_id,
                event_type,
                target_url,
                secret,
                request_body,
                status,
                attempt_count,
                last_error,
                next_attempt_at
            )
            VALUES ($1, $2, 'run.started', 'http://example.com/hook', 's', '{}'::jsonb, 'failed', 3, 'boom', now())
            RETURNING id
            """,
            uuid.UUID(sub_id),
            project_id,
        )
        assert delivery_id is not None
    finally:
        await conn.close()

    # Viewer can list deliveries
    resp = await service_client.get("/api/v1/webhooks/deliveries", headers=headers_viewer)
    assert resp.status == 200
    body = await resp.json()
    assert body["total"] >= 1
    ids = [d["id"] for d in body["deliveries"]]
    assert str(delivery_id) in ids

    # Owner can retry (sets status pending)
    resp = await service_client.post(
        f"/api/v1/webhooks/deliveries/{delivery_id}:retry",
        headers=headers_owner,
    )
    assert resp.status == 204

    # Filter by pending should include it
    resp = await service_client.get(
        "/api/v1/webhooks/deliveries?status=pending",
        headers=headers_viewer,
    )
    assert resp.status == 200
    body = await resp.json()
    ids = [d["id"] for d in body["deliveries"]]
    assert str(delivery_id) in ids

