"""Unit tests for the activate_scheduled_profiles worker task.

Uses mocked DB pool to avoid database dependency.
"""
from __future__ import annotations

import uuid
from datetime import datetime, timezone
from unittest.mock import AsyncMock, MagicMock, patch

import pytest

from experiment_service.workers import worker


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_pool_mock(activated_ids: list[uuid.UUID], deprecated_ids: list[uuid.UUID]) -> AsyncMock:
    """Build a pool mock whose conn.fetch returns the given UUID lists in order."""
    activated_rows = [{"id": uid, "sensor_id": uuid.uuid4(), "project_id": uuid.uuid4()} for uid in activated_ids]
    deprecated_rows = [{"id": uid} for uid in deprecated_ids]

    conn = AsyncMock()
    conn.fetch = AsyncMock(side_effect=[activated_rows, deprecated_rows])

    pool = AsyncMock()
    pool.acquire = MagicMock(return_value=_AsyncContextManager(conn))
    return pool


class _AsyncContextManager:
    """Minimal async context manager wrapper."""

    def __init__(self, value: object) -> None:
        self._value = value

    async def __aenter__(self) -> object:
        return self._value

    async def __aexit__(self, *_: object) -> None:
        pass


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_activate_scheduled_profiles_activates_due_drafts() -> None:
    """draft profiles with valid_from <= now must become active."""
    profile_id = uuid.uuid4()
    pool = _make_pool_mock(activated_ids=[profile_id], deprecated_ids=[])

    with patch(
        "experiment_service.workers.activate_scheduled_profiles.get_pool",
        new_callable=AsyncMock,
        return_value=pool,
    ):
        from experiment_service.workers.activate_scheduled_profiles import (
            activate_scheduled_profiles,
        )

        result = await activate_scheduled_profiles(datetime.now(timezone.utc))

    assert result == "activated=1"


@pytest.mark.asyncio
async def test_activate_scheduled_profiles_deprecates_expired() -> None:
    """active profiles with valid_to < now must become deprecated."""
    profile_id = uuid.uuid4()
    pool = _make_pool_mock(activated_ids=[], deprecated_ids=[profile_id])

    with patch(
        "experiment_service.workers.activate_scheduled_profiles.get_pool",
        new_callable=AsyncMock,
        return_value=pool,
    ):
        from experiment_service.workers.activate_scheduled_profiles import (
            activate_scheduled_profiles,
        )

        result = await activate_scheduled_profiles(datetime.now(timezone.utc))

    assert result == "deprecated=1"


@pytest.mark.asyncio
async def test_activate_scheduled_profiles_both_activated_and_deprecated() -> None:
    """When both activations and deprecations happen, both counts appear in summary."""
    pool = _make_pool_mock(
        activated_ids=[uuid.uuid4(), uuid.uuid4()],
        deprecated_ids=[uuid.uuid4()],
    )

    with patch(
        "experiment_service.workers.activate_scheduled_profiles.get_pool",
        new_callable=AsyncMock,
        return_value=pool,
    ):
        from experiment_service.workers.activate_scheduled_profiles import (
            activate_scheduled_profiles,
        )

        result = await activate_scheduled_profiles(datetime.now(timezone.utc))

    assert result is not None
    assert "activated=2" in result
    assert "deprecated=1" in result


@pytest.mark.asyncio
async def test_activate_scheduled_profiles_returns_none_when_nothing() -> None:
    """Worker returns None when no profiles qualify for activation or archival."""
    pool = _make_pool_mock(activated_ids=[], deprecated_ids=[])

    with patch(
        "experiment_service.workers.activate_scheduled_profiles.get_pool",
        new_callable=AsyncMock,
        return_value=pool,
    ):
        from experiment_service.workers.activate_scheduled_profiles import (
            activate_scheduled_profiles,
        )

        result = await activate_scheduled_profiles(datetime.now(timezone.utc))

    assert result is None


def test_worker_has_scheduled_profiles_task() -> None:
    """The module-level worker must include the activate_scheduled_profiles task (8th)."""
    task_names = [t.name for t in worker.tasks]
    assert "activate_scheduled_profiles" in task_names
    assert len(task_names) == 9
