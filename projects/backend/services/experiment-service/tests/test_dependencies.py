"""Unit tests for services/dependencies.py — no database required."""
from __future__ import annotations

import uuid
from unittest.mock import MagicMock

import pytest
from aiohttp import web

from experiment_service.services.dependencies import (
    UserContext,
    _parse_permissions,
    ensure_permission,
    ensure_project_context,
    resolve_project_id,
)


# ---------------------------------------------------------------------------
# _parse_permissions
# ---------------------------------------------------------------------------

def test_parse_permissions_empty_string():
    assert _parse_permissions("") == frozenset()


def test_parse_permissions_none():
    assert _parse_permissions(None) == frozenset()


def test_parse_permissions_single():
    assert _parse_permissions("experiments.create") == frozenset(["experiments.create"])


def test_parse_permissions_multiple():
    result = _parse_permissions("experiments.view,runs.create,project.members.view")
    assert result == frozenset(["experiments.view", "runs.create", "project.members.view"])


def test_parse_permissions_strips_whitespace():
    result = _parse_permissions("  experiments.view , runs.create  ")
    assert result == frozenset(["experiments.view", "runs.create"])


def test_parse_permissions_skips_empty_segments():
    result = _parse_permissions(",experiments.view,,runs.create,")
    assert result == frozenset(["experiments.view", "runs.create"])


# ---------------------------------------------------------------------------
# ensure_permission
# ---------------------------------------------------------------------------

def _make_user(
    *,
    is_superadmin: bool = False,
    system_permissions: frozenset[str] = frozenset(),
    project_permissions: frozenset[str] = frozenset(),
    project_id: uuid.UUID | None = None,
) -> UserContext:
    return UserContext(
        user_id=uuid.uuid4(),
        is_superadmin=is_superadmin,
        system_permissions=system_permissions,
        project_permissions=project_permissions,
        active_project_id=project_id or uuid.uuid4(),
    )


def test_ensure_permission_superadmin_always_allowed():
    user = _make_user(is_superadmin=True)
    # Should not raise for any permission
    ensure_permission(user, "experiments.create")
    ensure_permission(user, "project.settings.delete")
    ensure_permission(user, "nonexistent.permission")


def test_ensure_permission_project_permission_granted():
    user = _make_user(project_permissions=frozenset(["experiments.create", "experiments.view"]))
    ensure_permission(user, "experiments.create")  # no exception


def test_ensure_permission_system_permission_granted():
    user = _make_user(system_permissions=frozenset(["users.list", "audit.read"]))
    ensure_permission(user, "users.list")  # no exception


def test_ensure_permission_denied_raises_forbidden():
    user = _make_user(project_permissions=frozenset(["experiments.view"]))
    with pytest.raises(web.HTTPForbidden):
        ensure_permission(user, "experiments.create")


def test_ensure_permission_empty_permissions_raises_forbidden():
    user = _make_user()
    with pytest.raises(web.HTTPForbidden):
        ensure_permission(user, "experiments.view")


def test_ensure_permission_superadmin_not_in_permissions():
    # is_superadmin=False, permission not in either set → forbidden
    user = _make_user(
        is_superadmin=False,
        project_permissions=frozenset(["runs.create"]),
        system_permissions=frozenset(),
    )
    with pytest.raises(web.HTTPForbidden):
        ensure_permission(user, "experiments.create")


# ---------------------------------------------------------------------------
# ensure_project_context
# ---------------------------------------------------------------------------

def test_ensure_project_context_returns_project_id():
    pid = uuid.uuid4()
    user = _make_user(project_id=pid)
    assert ensure_project_context(user) == pid


def test_ensure_project_context_none_raises_bad_request():
    user = UserContext(
        user_id=uuid.uuid4(),
        is_superadmin=False,
        system_permissions=frozenset(),
        project_permissions=frozenset(),
        active_project_id=None,
    )
    with pytest.raises(web.HTTPBadRequest):
        ensure_project_context(user)


# ---------------------------------------------------------------------------
# resolve_project_id
# ---------------------------------------------------------------------------

def test_resolve_project_id_from_string():
    pid = uuid.uuid4()
    user = _make_user()
    result = resolve_project_id(user, str(pid))
    assert result == pid


def test_resolve_project_id_from_active_project_id():
    pid = uuid.uuid4()
    user = _make_user(project_id=pid)
    result = resolve_project_id(user, None)
    assert result == pid


def test_resolve_project_id_none_and_no_active_raises_bad_request():
    user = UserContext(
        user_id=uuid.uuid4(),
        is_superadmin=False,
        system_permissions=frozenset(),
        project_permissions=frozenset(),
        active_project_id=None,
    )
    with pytest.raises(web.HTTPBadRequest):
        resolve_project_id(user, None)


def test_resolve_project_id_invalid_uuid_raises_bad_request():
    user = _make_user()
    with pytest.raises(web.HTTPBadRequest):
        resolve_project_id(user, "not-a-uuid")


# ---------------------------------------------------------------------------
# UserContext: make_headers integration check (via tests/utils.py)
# ---------------------------------------------------------------------------

from tests.utils import ROLE_PERMISSIONS, make_headers


def test_make_headers_owner_has_create_permission():
    pid = uuid.uuid4()
    headers = make_headers(pid, role="owner")
    assert headers["X-User-Is-Superadmin"] == "false"
    assert "experiments.create" in headers["X-User-Permissions"]
    assert "project.settings.delete" in headers["X-User-Permissions"]


def test_make_headers_viewer_has_no_create_permission():
    pid = uuid.uuid4()
    headers = make_headers(pid, role="viewer")
    assert "experiments.create" not in headers["X-User-Permissions"]
    assert "experiments.view" in headers["X-User-Permissions"]


def test_make_headers_superadmin():
    headers = make_headers(None, superadmin=True)
    assert headers["X-User-Is-Superadmin"] == "true"
    assert "X-User-Permissions" not in headers


def test_make_headers_no_project_id():
    headers = make_headers(None, role="editor")
    assert "X-Project-Id" not in headers
    assert "X-User-Permissions" in headers
