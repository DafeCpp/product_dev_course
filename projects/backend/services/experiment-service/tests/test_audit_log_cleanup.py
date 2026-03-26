"""Unit tests for audit_log_cleanup worker."""
from __future__ import annotations

from contextlib import asynccontextmanager
from datetime import datetime, timezone
from unittest.mock import AsyncMock, MagicMock, patch

import pytest

from experiment_service.workers import worker


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_pool(run_events_deleted: int, capture_session_events_deleted: int) -> MagicMock:
    """Return a mock pool with acquire() context manager."""
    conn = AsyncMock()
    conn.fetchval = AsyncMock(
        side_effect=[run_events_deleted, capture_session_events_deleted]
    )

    pool = MagicMock()

    @asynccontextmanager  # type: ignore[misc]
    async def _acquire() -> object:
        yield conn

    pool.acquire = _acquire
    pool._conn = conn  # expose for assertions
    return pool


@pytest.fixture
def mock_pool_audit():
    with patch(
        "experiment_service.workers.audit_log_cleanup.get_pool",
        new_callable=AsyncMock,
    ) as mock_get_pool:
        yield mock_get_pool


# ---------------------------------------------------------------------------
# audit_log_cleanup
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_audit_log_cleanup_deletes_old_records(mock_pool_audit):
    now = datetime.now(timezone.utc)
    mock_pool_audit.return_value = _make_pool(
        run_events_deleted=7,
        capture_session_events_deleted=3,
    )

    from experiment_service.workers.audit_log_cleanup import audit_log_cleanup
    result = await audit_log_cleanup(now)

    assert result == "deleted_run_events=7 deleted_capture_session_events=3"


@pytest.mark.asyncio
async def test_audit_log_cleanup_returns_none_when_nothing_deleted(mock_pool_audit):
    now = datetime.now(timezone.utc)
    mock_pool_audit.return_value = _make_pool(
        run_events_deleted=0,
        capture_session_events_deleted=0,
    )

    from experiment_service.workers.audit_log_cleanup import audit_log_cleanup
    result = await audit_log_cleanup(now)

    assert result is None


@pytest.mark.asyncio
async def test_audit_log_cleanup_cutoff_is_before_now(mock_pool_audit):
    now = datetime.now(timezone.utc)
    pool = _make_pool(run_events_deleted=0, capture_session_events_deleted=0)
    mock_pool_audit.return_value = pool

    from experiment_service.workers.audit_log_cleanup import audit_log_cleanup
    await audit_log_cleanup(now)

    conn = pool._conn
    first_cutoff = conn.fetchval.call_args_list[0][0][1]
    second_cutoff = conn.fetchval.call_args_list[1][0][1]
    assert isinstance(first_cutoff, datetime)
    assert isinstance(second_cutoff, datetime)
    assert first_cutoff < now
    assert second_cutoff < now


@pytest.mark.asyncio
async def test_audit_log_cleanup_partial_deletion_returns_summary(mock_pool_audit):
    """Returns summary even when only one table has deletions."""
    now = datetime.now(timezone.utc)
    mock_pool_audit.return_value = _make_pool(
        run_events_deleted=5,
        capture_session_events_deleted=0,
    )

    from experiment_service.workers.audit_log_cleanup import audit_log_cleanup
    result = await audit_log_cleanup(now)

    assert result == "deleted_run_events=5 deleted_capture_session_events=0"


# ---------------------------------------------------------------------------
# Worker assembly
# ---------------------------------------------------------------------------

def test_worker_has_audit_log_cleanup_task():
    task_names = [t.name for t in worker.tasks]
    assert "audit_log_cleanup" in task_names
    assert len(task_names) == 8
