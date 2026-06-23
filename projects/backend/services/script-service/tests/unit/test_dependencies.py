"""Unit tests for script_service.dependencies (RBAC helpers and DI)."""
from __future__ import annotations

from unittest.mock import AsyncMock, patch
from uuid import UUID

import pytest
from aiohttp import web
from aiohttp.test_utils import make_mocked_request

from script_service.dependencies import (
    UserContext,
    _parse_permissions,
    ensure_permission,
    extract_user,
    get_execution_dispatcher,
    get_script_manager,
)

_USER_ID = "550e8400-e29b-41d4-a716-446655440001"


def _make_request(headers: dict[str, str] | None = None) -> web.Request:
    return make_mocked_request("GET", "/", headers=headers or {})


# ===========================================================================
# _parse_permissions
# ===========================================================================

class TestParsePermissions:
    def test_none_returns_empty_frozenset(self):
        assert _parse_permissions(None) == frozenset()

    def test_empty_string_returns_empty_frozenset(self):
        assert _parse_permissions("") == frozenset()

    def test_single_value(self):
        assert _parse_permissions("scripts.manage") == frozenset({"scripts.manage"})

    def test_multiple_values_comma_separated(self):
        result = _parse_permissions("scripts.manage,scripts.execute,scripts.view_logs")
        assert result == frozenset({"scripts.manage", "scripts.execute", "scripts.view_logs"})

    def test_strips_surrounding_whitespace(self):
        result = _parse_permissions("  scripts.manage  ,  scripts.execute  ")
        assert result == frozenset({"scripts.manage", "scripts.execute"})

    def test_drops_blank_segments(self):
        result = _parse_permissions("scripts.manage,,  ,scripts.execute,")
        assert result == frozenset({"scripts.manage", "scripts.execute"})

    def test_deduplicates(self):
        result = _parse_permissions("scripts.manage,scripts.manage,scripts.execute")
        assert result == frozenset({"scripts.manage", "scripts.execute"})


# ===========================================================================
# extract_user
# ===========================================================================

class TestExtractUser:
    def test_missing_user_id_header_raises_401(self):
        request = _make_request()
        with pytest.raises(web.HTTPUnauthorized):
            extract_user(request)

    def test_invalid_user_id_uuid_raises_400(self):
        request = _make_request({"X-User-Id": "not-a-uuid"})
        with pytest.raises(web.HTTPBadRequest):
            extract_user(request)

    def test_minimal_valid_headers_yield_default_context(self):
        request = _make_request({"X-User-Id": _USER_ID})
        ctx = extract_user(request)

        assert ctx.user_id == UUID(_USER_ID)
        assert ctx.permissions == frozenset()
        assert ctx.system_permissions == frozenset()
        assert ctx.is_superadmin is False

    def test_is_superadmin_true_lowercase(self):
        request = _make_request({"X-User-Id": _USER_ID, "X-User-Is-Superadmin": "true"})
        assert extract_user(request).is_superadmin is True

    def test_is_superadmin_true_uppercase(self):
        request = _make_request({"X-User-Id": _USER_ID, "X-User-Is-Superadmin": "TRUE"})
        assert extract_user(request).is_superadmin is True

    def test_is_superadmin_with_surrounding_whitespace(self):
        request = _make_request({"X-User-Id": _USER_ID, "X-User-Is-Superadmin": "  True  "})
        assert extract_user(request).is_superadmin is True

    def test_is_superadmin_false_value(self):
        request = _make_request({"X-User-Id": _USER_ID, "X-User-Is-Superadmin": "false"})
        assert extract_user(request).is_superadmin is False

    def test_is_superadmin_other_value_treated_as_false(self):
        # "1" is not "true" → must default to False, no implicit truthy parsing.
        request = _make_request({"X-User-Id": _USER_ID, "X-User-Is-Superadmin": "1"})
        assert extract_user(request).is_superadmin is False

    def test_permissions_parsed_into_frozenset(self):
        request = _make_request(
            {"X-User-Id": _USER_ID, "X-User-Permissions": "projects.read, scripts.manage"}
        )
        ctx = extract_user(request)
        assert ctx.permissions == frozenset({"projects.read", "scripts.manage"})

    def test_system_permissions_parsed_into_frozenset(self):
        request = _make_request(
            {"X-User-Id": _USER_ID, "X-User-System-Permissions": "scripts.execute"}
        )
        ctx = extract_user(request)
        assert ctx.system_permissions == frozenset({"scripts.execute"})

    def test_user_context_is_immutable_frozenset(self):
        request = _make_request(
            {"X-User-Id": _USER_ID, "X-User-Permissions": "scripts.manage"}
        )
        ctx = extract_user(request)
        # frozenset is hashable & immutable — sanity check
        assert isinstance(ctx.permissions, frozenset)
        assert isinstance(ctx.system_permissions, frozenset)


# ===========================================================================
# ensure_permission
# ===========================================================================

class TestEnsurePermission:
    def _ctx(self, **overrides) -> UserContext:
        defaults: dict = dict(
            user_id=UUID(_USER_ID),
            permissions=frozenset(),
            system_permissions=frozenset(),
            is_superadmin=False,
        )
        defaults.update(overrides)
        return UserContext(**defaults)

    def test_superadmin_bypasses_check_when_no_permissions(self):
        ensure_permission(self._ctx(is_superadmin=True), "scripts.manage")  # no raise

    def test_superadmin_bypasses_check_for_unknown_permission(self):
        ensure_permission(self._ctx(is_superadmin=True), "totally.fake.permission")  # no raise

    def test_permission_in_user_permissions_passes(self):
        ctx = self._ctx(permissions=frozenset({"scripts.manage"}))
        ensure_permission(ctx, "scripts.manage")  # no raise

    def test_permission_in_system_permissions_passes(self):
        ctx = self._ctx(system_permissions=frozenset({"scripts.manage"}))
        ensure_permission(ctx, "scripts.manage")  # no raise

    def test_missing_permission_raises_forbidden(self):
        with pytest.raises(web.HTTPForbidden):
            ensure_permission(self._ctx(), "scripts.manage")

    def test_forbidden_reason_mentions_missing_permission(self):
        with pytest.raises(web.HTTPForbidden) as exc_info:
            ensure_permission(self._ctx(), "scripts.execute")
        assert "scripts.execute" in (exc_info.value.reason or "")

    def test_unrelated_permission_does_not_grant_access(self):
        ctx = self._ctx(permissions=frozenset({"projects.read"}))
        with pytest.raises(web.HTTPForbidden):
            ensure_permission(ctx, "scripts.manage")


# ===========================================================================
# get_script_manager — caching behavior
# ===========================================================================

class TestGetScriptManager:
    @patch("script_service.dependencies.get_pool", new_callable=AsyncMock)
    async def test_first_call_creates_manager(self, mock_pool):
        mock_pool.return_value = AsyncMock(name="pool")
        request = _make_request({"X-User-Id": _USER_ID})

        manager = await get_script_manager(request)

        assert manager is not None
        mock_pool.assert_awaited_once()

    @patch("script_service.dependencies.get_pool", new_callable=AsyncMock)
    async def test_second_call_returns_cached_instance(self, mock_pool):
        mock_pool.return_value = AsyncMock(name="pool")
        request = _make_request({"X-User-Id": _USER_ID})

        first = await get_script_manager(request)
        second = await get_script_manager(request)

        assert first is second
        # Pool only fetched once — second call hit the cache.
        mock_pool.assert_awaited_once()


# ===========================================================================
# get_execution_dispatcher — caching behavior
# ===========================================================================

class TestGetExecutionDispatcher:
    @patch("script_service.dependencies.get_pool", new_callable=AsyncMock)
    async def test_first_call_creates_dispatcher(self, mock_pool):
        mock_pool.return_value = AsyncMock(name="pool")
        request = _make_request({"X-User-Id": _USER_ID})

        dispatcher = await get_execution_dispatcher(request)

        assert dispatcher is not None
        mock_pool.assert_awaited_once()

    @patch("script_service.dependencies.get_pool", new_callable=AsyncMock)
    async def test_second_call_returns_cached_instance(self, mock_pool):
        mock_pool.return_value = AsyncMock(name="pool")
        request = _make_request({"X-User-Id": _USER_ID})

        first = await get_execution_dispatcher(request)
        second = await get_execution_dispatcher(request)

        assert first is second
        mock_pool.assert_awaited_once()
