"""Unit tests for experiment_service.workers task functions.

Uses mocked repositories to avoid database dependency.
"""
from __future__ import annotations

from datetime import datetime, timezone
from unittest.mock import AsyncMock, patch

import pytest

from experiment_service.workers import worker


# ---------------------------------------------------------------------------
# idempotency_cleanup
# ---------------------------------------------------------------------------

@pytest.fixture
def mock_pool_idempotency():
    with patch(
        "experiment_service.workers.idempotency_cleanup.get_pool",
        new_callable=AsyncMock,
        return_value=AsyncMock(),
    ):
        yield


@pytest.mark.asyncio
async def test_idempotency_cleanup_returns_summary(mock_pool_idempotency):
    now = datetime.now(timezone.utc)
    with patch(
        "experiment_service.workers.idempotency_cleanup.IdempotencyRepository"
    ) as MockRepo:
        instance = MockRepo.return_value
        instance.delete_expired = AsyncMock(return_value=5)

        from experiment_service.workers.idempotency_cleanup import idempotency_cleanup
        result = await idempotency_cleanup(now)

    assert result == "deleted=5"
    instance.delete_expired.assert_awaited_once()
    call_arg = instance.delete_expired.call_args[0][0]
    assert isinstance(call_arg, datetime)
    assert call_arg < now


@pytest.mark.asyncio
async def test_idempotency_cleanup_returns_none_when_nothing_deleted(mock_pool_idempotency):
    now = datetime.now(timezone.utc)
    with patch(
        "experiment_service.workers.idempotency_cleanup.IdempotencyRepository"
    ) as MockRepo:
        instance = MockRepo.return_value
        instance.delete_expired = AsyncMock(return_value=0)

        from experiment_service.workers.idempotency_cleanup import idempotency_cleanup
        result = await idempotency_cleanup(now)

    assert result is None


# ---------------------------------------------------------------------------
# stale_session_cleanup
# ---------------------------------------------------------------------------

@pytest.fixture
def mock_pool_stale():
    with patch(
        "experiment_service.workers.stale_session_cleanup.get_pool",
        new_callable=AsyncMock,
        return_value=AsyncMock(),
    ):
        yield


@pytest.mark.asyncio
async def test_stale_session_cleanup_returns_summary(mock_pool_stale):
    now = datetime.now(timezone.utc)
    with patch(
        "experiment_service.workers.stale_session_cleanup.CaptureSessionRepository"
    ) as MockRepo:
        instance = MockRepo.return_value
        instance.fail_stale_sessions = AsyncMock(return_value=3)

        from experiment_service.workers.stale_session_cleanup import stale_session_cleanup
        result = await stale_session_cleanup(now)

    assert result == "failed_sessions=3"
    instance.fail_stale_sessions.assert_awaited_once()


@pytest.mark.asyncio
async def test_stale_session_cleanup_returns_none_when_no_stale(mock_pool_stale):
    now = datetime.now(timezone.utc)
    with patch(
        "experiment_service.workers.stale_session_cleanup.CaptureSessionRepository"
    ) as MockRepo:
        instance = MockRepo.return_value
        instance.fail_stale_sessions = AsyncMock(return_value=0)

        from experiment_service.workers.stale_session_cleanup import stale_session_cleanup
        result = await stale_session_cleanup(now)

    assert result is None


# ---------------------------------------------------------------------------
# webhook_reclaim_stuck
# ---------------------------------------------------------------------------

@pytest.fixture
def mock_pool_reclaim():
    with patch(
        "experiment_service.workers.webhook_reclaim.get_pool",
        new_callable=AsyncMock,
        return_value=AsyncMock(),
    ):
        yield


@pytest.mark.asyncio
async def test_webhook_reclaim_stuck_returns_summary(mock_pool_reclaim):
    now = datetime.now(timezone.utc)
    with patch(
        "experiment_service.workers.webhook_reclaim.WebhookDeliveryRepository"
    ) as MockRepo:
        instance = MockRepo.return_value
        instance.reclaim_stuck = AsyncMock(return_value=2)

        from experiment_service.workers.webhook_reclaim import webhook_reclaim_stuck
        result = await webhook_reclaim_stuck(now)

    assert result == "reclaimed=2"


# ---------------------------------------------------------------------------
# webhook_purge_succeeded
# ---------------------------------------------------------------------------

@pytest.fixture
def mock_pool_purge():
    with patch(
        "experiment_service.workers.webhook_purge.get_pool",
        new_callable=AsyncMock,
        return_value=AsyncMock(),
    ):
        yield


@pytest.mark.asyncio
async def test_webhook_purge_succeeded_returns_summary(mock_pool_purge):
    now = datetime.now(timezone.utc)
    with patch(
        "experiment_service.workers.webhook_purge.WebhookDeliveryRepository"
    ) as MockRepo:
        instance = MockRepo.return_value
        instance.delete_old_succeeded = AsyncMock(return_value=10)

        from experiment_service.workers.webhook_purge import webhook_purge_succeeded
        result = await webhook_purge_succeeded(now)

    assert result == "purged=10"


# ---------------------------------------------------------------------------
# worker assembly
# ---------------------------------------------------------------------------

def test_worker_instance_has_correct_tasks():
    """The module-level worker should have all 4 tasks registered."""
    task_names = [t.name for t in worker.tasks]
    assert "idempotency_cleanup" in task_names
    assert "stale_session_cleanup" in task_names
    assert "webhook_reclaim_stuck" in task_names
    assert "webhook_purge_succeeded" in task_names
    assert len(task_names) == 4
