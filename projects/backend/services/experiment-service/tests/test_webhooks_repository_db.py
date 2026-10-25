"""Webhook subscription and delivery repositories against a real database."""
from __future__ import annotations

import json
import uuid
from datetime import UTC, datetime, timedelta

import asyncpg
import pytest

from experiment_service.core.exceptions import NotFoundError
from experiment_service.repositories.webhooks import (
    WebhookDeliveryRepository,
    WebhookSubscriptionRepository,
)


@pytest.fixture
async def db_pool(pgsql):
    pool = await asyncpg.create_pool(dsn=pgsql["experiment_service"].conninfo.get_uri())
    try:
        yield pool
    finally:
        await pool.close()


@pytest.fixture
def subscriptions(db_pool) -> WebhookSubscriptionRepository:
    return WebhookSubscriptionRepository(db_pool)


@pytest.fixture
def deliveries(db_pool) -> WebhookDeliveryRepository:
    return WebhookDeliveryRepository(db_pool)


async def make_subscription(subscriptions, project_id, events=("run.created",)):
    return await subscriptions.create(
        project_id=project_id, target_url="https://hook.test/a", event_types=list(events), secret="s3cret"
    )


async def enqueue(deliveries, subscription, **extra):
    return await deliveries.enqueue(
        subscription_id=subscription.id,
        project_id=subscription.project_id,
        event_type="run.created",
        target_url=subscription.target_url,
        secret=subscription.secret,
        request_body={"n": 1},
        **extra,
    )


async def test_subscription_lifecycle(subscriptions, db_pool):
    project_id = uuid.uuid4()
    first = await make_subscription(subscriptions, project_id)
    second = await make_subscription(subscriptions, project_id, events=("run.failed",))

    items, total = await subscriptions.list_by_project(project_id, limit=1)
    assert (len(items), total) == (1, 2)
    items, total = await subscriptions.list_by_project(project_id, offset=10)
    assert (items, total) == ([], 2)

    matching = await subscriptions.list_active_matching(project_id, "run.created")
    assert [s.id for s in matching] == [first.id]
    await db_pool.execute("UPDATE webhook_subscriptions SET is_active = false WHERE id = $1", first.id)
    inactive = await subscriptions.list_active_matching(project_id, "run.created")
    assert inactive == []

    await subscriptions.delete(project_id, second.id)
    with pytest.raises(NotFoundError):
        await subscriptions.delete(project_id, second.id)


async def test_enqueue_dedup_and_claim(subscriptions, deliveries):
    subscription = await make_subscription(subscriptions, uuid.uuid4())

    plain = await enqueue(deliveries, subscription)
    assert plain.request_body == {"n": 1}
    first = await enqueue(deliveries, subscription, dedup_key="k1")
    again = await enqueue(deliveries, subscription, dedup_key="k1")
    assert first.id == again.id

    claimed = await deliveries.claim_due_pending(limit=10)
    assert {d.id for d in claimed} >= {plain.id, first.id}
    assert all(d.status == "in_progress" and d.attempt_count == 1 for d in claimed if d.id in {plain.id, first.id})


async def test_delivery_listing_retry_and_attempts(subscriptions, deliveries):
    project_id = uuid.uuid4()
    subscription = await make_subscription(subscriptions, project_id)
    first = await enqueue(deliveries, subscription)
    second = await enqueue(deliveries, subscription)

    await deliveries.mark_attempt(
        first.id, success=False, status="dead_lettered", last_error="boom", next_attempt_at=None, attempt_count=5
    )
    await deliveries.mark_attempt(
        second.id,
        success=True,
        status="succeeded",
        last_error=None,
        next_attempt_at=datetime.now(UTC) + timedelta(hours=1),
        attempt_count=1,
    )

    items, total = await deliveries.list_by_project(project_id)
    assert total == 2
    items, total = await deliveries.list_by_project(project_id, status="succeeded")
    assert ([d.id for d in items], total) == ([second.id], 1)
    items, total = await deliveries.list_by_project(project_id, offset=10)
    assert (items, total) == ([], 2)
    items, total = await deliveries.list_by_project(project_id, status="dead_lettered", offset=10)
    assert (items, total) == ([], 1)

    await deliveries.retry(project_id, first.id)
    retried, _ = await deliveries.list_by_project(project_id, status="pending")
    assert [d.id for d in retried] == [first.id] and retried[0].last_error is None
    with pytest.raises(NotFoundError):
        await deliveries.retry(project_id, uuid.uuid4())


async def test_reclaim_and_purge(subscriptions, deliveries, db_pool):
    subscription = await make_subscription(subscriptions, uuid.uuid4())
    stuck = await enqueue(deliveries, subscription)
    done = await enqueue(deliveries, subscription)
    await db_pool.execute(
        "UPDATE webhook_deliveries SET status = 'in_progress', locked_at = now() - INTERVAL '1 hour' WHERE id = $1",
        stuck.id,
    )
    await db_pool.execute(
        "UPDATE webhook_deliveries SET status = 'succeeded', created_at = now() - INTERVAL '10 days' WHERE id = $1",
        done.id,
    )

    reclaimed = await deliveries.reclaim_stuck(datetime.now(UTC) - timedelta(minutes=5))
    purged = await deliveries.delete_old_succeeded(datetime.now(UTC) - timedelta(days=1))
    purged_again = await deliveries.delete_old_succeeded(datetime.now(UTC) - timedelta(days=1))
    assert (reclaimed, purged, purged_again) == (1, 1, 0)


def test_normalize_decodes_json_strings():
    payload = WebhookDeliveryRepository._normalize({"request_body": json.dumps({"a": 1})})
    assert payload["request_body"] == {"a": 1}
    assert WebhookDeliveryRepository._normalize({"request_body": {"a": 1}})["request_body"] == {"a": 1}
