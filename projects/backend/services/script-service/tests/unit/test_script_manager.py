"""Unit tests for ScriptManager service layer."""
from __future__ import annotations

from datetime import datetime, timezone
from unittest.mock import AsyncMock
from uuid import UUID, uuid4

import pytest

from script_service.domain.models import Script, ScriptType
from script_service.services.script_manager import ScriptManager, ScriptNotFoundError


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

_NOW = datetime(2024, 1, 1, tzinfo=timezone.utc)
_USER_ID = UUID("550e8400-e29b-41d4-a716-446655440001")


def _make_script(**overrides) -> Script:
    defaults = dict(
        id=uuid4(),
        name="test-script",
        description=None,
        target_service="experiment-service",
        script_type=ScriptType.python,
        script_body="print('ok')",
        parameters_schema={},
        timeout_sec=30,
        is_active=True,
        created_by=_USER_ID,
        created_at=_NOW,
        updated_at=_NOW,
    )
    defaults.update(overrides)
    return Script(**defaults)


def _make_repo(**method_overrides) -> AsyncMock:
    """Return an AsyncMock that behaves like ScriptRepository."""
    repo = AsyncMock()
    for name, return_value in method_overrides.items():
        getattr(repo, name).return_value = return_value
    return repo


# ===========================================================================
# TestScriptManager
# ===========================================================================

class TestScriptManager:
    async def test_create_script_calls_repo_create_with_correct_args(self):
        script = _make_script()
        repo = _make_repo(create=script)
        manager = ScriptManager(repo)

        result = await manager.create_script(
            name="test-script",
            description=None,
            target_service="experiment-service",
            script_type=ScriptType.python,
            script_body="print('ok')",
            parameters_schema={},
            timeout_sec=30,
            created_by=_USER_ID,
        )

        repo.create.assert_awaited_once_with(
            name="test-script",
            description=None,
            target_service="experiment-service",
            script_type=ScriptType.python,
            script_body="print('ok')",
            parameters_schema={},
            timeout_sec=30,
            created_by=_USER_ID,
        )
        assert result is script

    async def test_get_script_not_found_raises_script_not_found_error(self):
        repo = _make_repo(get_by_id=None)
        manager = ScriptManager(repo)
        missing_id = uuid4()

        with pytest.raises(ScriptNotFoundError, match=str(missing_id)):
            await manager.get_script(missing_id)

    async def test_get_script_found_returns_script(self):
        script = _make_script()
        repo = _make_repo(get_by_id=script)
        manager = ScriptManager(repo)

        result = await manager.get_script(script.id)

        repo.get_by_id.assert_awaited_once_with(script.id)
        assert result is script

    async def test_list_scripts_returns_repo_result(self):
        scripts = [_make_script(name=f"s{i}") for i in range(3)]
        repo = _make_repo(list=scripts)
        manager = ScriptManager(repo)

        result = await manager.list_scripts(target_service="experiment-service", is_active=True)

        repo.list.assert_awaited_once_with(
            target_service="experiment-service",
            is_active=True,
            limit=50,
            offset=0,
        )
        assert result == scripts

    async def test_update_script_calls_repo_update_and_returns_updated(self):
        updated = _make_script(name="new-name")
        repo = _make_repo(update=updated)
        manager = ScriptManager(repo)

        result = await manager.update_script(updated.id, name="new-name")

        repo.update.assert_awaited_once_with(updated.id, name="new-name")
        assert result is updated

    async def test_update_script_not_found_raises_script_not_found_error(self):
        repo = _make_repo(update=None)
        manager = ScriptManager(repo)
        missing_id = uuid4()

        with pytest.raises(ScriptNotFoundError, match=str(missing_id)):
            await manager.update_script(missing_id, name="ghost")

    async def test_delete_script_calls_soft_delete(self):
        repo = _make_repo(soft_delete=True)
        manager = ScriptManager(repo)
        script_id = uuid4()

        await manager.delete_script(script_id)

        repo.soft_delete.assert_awaited_once_with(script_id)

    async def test_delete_script_not_found_raises_script_not_found_error(self):
        repo = _make_repo(soft_delete=False)
        manager = ScriptManager(repo)
        missing_id = uuid4()

        with pytest.raises(ScriptNotFoundError, match=str(missing_id)):
            await manager.delete_script(missing_id)
