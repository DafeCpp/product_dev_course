"""Unit tests for auth-service audit_log_cleanup worker."""
from __future__ import annotations

from contextlib import asynccontextmanager
from datetime import datetime, timezone
from unittest.mock import AsyncMock, MagicMock, patch

import pytest

from auth_service.workers import worker


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_pool(deleted: int) -> MagicMock:
    """Return a mock pool with acquire() context manager."""
    conn = AsyncMock()
    conn.fetchval = AsyncMock(return_value=deleted)

    pool = MagicMock()

    @asynccontextmanager  # type: ignore[misc]
    async def _acquire() -> object:
        yield conn

    pool.acquire = _acquire
    pool._conn = conn
    return pool


@pytest.fixture
def mock_pool_audit():
    with patch(
        "auth_service.workers.audit_log_cleanup.get_pool",
        new_callable=AsyncMock,
    ) as mock_get_pool:
        yield mock_get_pool


# ---------------------------------------------------------------------------
# audit_log_cleanup
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_audit_log_cleanup_returns_summary(mock_pool_audit):
    now = datetime.now(timezone.utc)
    mock_pool_audit.return_value = _make_pool(deleted=42)

    from auth_service.workers.audit_log_cleanup import audit_log_cleanup
    result = await audit_log_cleanup(now)

    assert result == "deleted=42"


@pytest.mark.asyncio
async def test_audit_log_cleanup_returns_none_when_nothing_deleted(mock_pool_audit):
    now = datetime.now(timezone.utc)
    mock_pool_audit.return_value = _make_pool(deleted=0)

    from auth_service.workers.audit_log_cleanup import audit_log_cleanup
    result = await audit_log_cleanup(now)

    assert result is None


@pytest.mark.asyncio
async def test_audit_log_cleanup_cutoff_is_before_now(mock_pool_audit):
    now = datetime.now(timezone.utc)
    pool = _make_pool(deleted=0)
    mock_pool_audit.return_value = pool

    from auth_service.workers.audit_log_cleanup import audit_log_cleanup
    await audit_log_cleanup(now)

    cutoff = pool._conn.fetchval.call_args[0][1]
    assert isinstance(cutoff, datetime)
    assert cutoff < now


# ---------------------------------------------------------------------------
# Worker assembly
# ---------------------------------------------------------------------------

def test_worker_has_audit_log_cleanup_task():
    task_names = [t.name for t in worker.tasks]
    assert "audit_log_cleanup" in task_names
    assert "token_cleanup" in task_names
