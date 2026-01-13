from __future__ import annotations

import uuid

import asyncpg
import pytest

from experiment_service.repositories.webhooks import WebhookDeliveryRepository, WebhookSubscriptionRepository


@pytest.fixture
async def db_pool(pgsql):
    conninfo = pgsql["experiment_service"].conninfo
    pool = await asyncpg.create_pool(dsn=conninfo.get_uri())
    try:
        yield pool
    finally:
        await pool.close()


@pytest.mark.asyncio
async def test_webhook_enqueue_dedup_key_returns_existing(db_pool):
    subs_repo = WebhookSubscriptionRepository(db_pool)
    deliveries_repo = WebhookDeliveryRepository(db_pool)

    project_id = uuid.uuid4()
    sub = await subs_repo.create(
        project_id=project_id,
        target_url="http://example.com/hook",
        event_types=["run.started"],
        secret="s",
    )

    dedup_key = f"{sub.id}:run.started:deadbeef"
    body = {"event_type": "run.started", "payload": {"run_id": str(uuid.uuid4())}}

    d1 = await deliveries_repo.enqueue(
        subscription_id=sub.id,
        project_id=project_id,
        event_type="run.started",
        target_url=sub.target_url,
        secret=sub.secret,
        request_body=body,
        dedup_key=dedup_key,
    )
    d2 = await deliveries_repo.enqueue(
        subscription_id=sub.id,
        project_id=project_id,
        event_type="run.started",
        target_url=sub.target_url,
        secret=sub.secret,
        request_body=body,
        dedup_key=dedup_key,
    )

    assert d1.id == d2.id

    async with db_pool.acquire() as conn:
        count = await conn.fetchval(
            "SELECT COUNT(*) FROM webhook_deliveries WHERE dedup_key = $1",
            dedup_key,
        )
        assert int(count) == 1


@pytest.mark.asyncio
async def test_webhook_claim_marks_in_progress_and_increments_attempt(db_pool):
    subs_repo = WebhookSubscriptionRepository(db_pool)
    deliveries_repo = WebhookDeliveryRepository(db_pool)

    project_id = uuid.uuid4()
    sub = await subs_repo.create(
        project_id=project_id,
        target_url="http://example.com/hook",
        event_types=["capture_session.created"],
        secret=None,
    )

    # Insert a pending delivery due now.
    d = await deliveries_repo.enqueue(
        subscription_id=sub.id,
        project_id=project_id,
        event_type="capture_session.created",
        target_url=sub.target_url,
        secret=sub.secret,
        request_body={"event_type": "capture_session.created", "payload": {"x": 1}},
        dedup_key=f"{sub.id}:capture_session.created:{uuid.uuid4()}",
    )

    # Claim should set in_progress, locked_at, attempt_count = 1.
    claimed = await deliveries_repo.claim_due_pending(limit=10)
    ids = [x.id for x in claimed]
    assert d.id in ids
    got = next(x for x in claimed if x.id == d.id)
    assert got.status == "in_progress"
    assert got.attempt_count == 1
    assert got.locked_at is not None

    # Mark back to pending and ensure locked_at cleared.
    await deliveries_repo.mark_attempt(
        got.id,
        success=False,
        status="pending",
        last_error="boom",
        next_attempt_at=None,
        attempt_count=got.attempt_count,
    )

    async with db_pool.acquire() as conn:
        row = await conn.fetchrow(
            "SELECT status, locked_at, attempt_count, last_error FROM webhook_deliveries WHERE id = $1",
            got.id,
        )
        assert row is not None
        assert row["status"] == "pending"
        assert row["locked_at"] is None
        assert int(row["attempt_count"]) == 1
        assert row["last_error"] == "boom"

