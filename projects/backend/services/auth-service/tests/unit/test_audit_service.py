"""Unit tests for AuditService."""
from __future__ import annotations

from datetime import datetime, timezone
from unittest.mock import AsyncMock
from uuid import uuid4

import pytest

from auth_service.domain.models import AuditAction, AuditEntry, ScopeType
from auth_service.services.audit import AuditService


def _make_entry(**kwargs) -> AuditEntry:
    defaults = {
        "id": uuid4(),
        "timestamp": datetime.now(timezone.utc),
        "actor_id": uuid4(),
        "action": AuditAction.LOGIN,
        "scope_type": ScopeType.SYSTEM,
        "scope_id": None,
        "target_type": None,
        "target_id": None,
        "details": {},
        "ip_address": None,
        "user_agent": None,
    }
    defaults.update(kwargs)
    return AuditEntry(**defaults)


@pytest.fixture
def audit_repo():
    repo = AsyncMock()
    repo.log = AsyncMock(return_value=_make_entry())
    repo.query = AsyncMock(return_value=[])
    return repo


@pytest.fixture
def audit_service(audit_repo):
    return AuditService(audit_repo)


class TestAuditServiceLogAction:
    @pytest.mark.asyncio
    async def test_log_action_calls_repo(self, audit_service, audit_repo):
        actor_id = uuid4()
        entry = await audit_service.log_action(
            actor_id=actor_id,
            action=AuditAction.LOGIN,
            scope_type=ScopeType.SYSTEM,
            ip_address="127.0.0.1",
            user_agent="Mozilla/5.0",
        )
        audit_repo.log.assert_called_once_with(
            actor_id=actor_id,
            action=AuditAction.LOGIN,
            scope_type=ScopeType.SYSTEM,
            scope_id=None,
            target_type=None,
            target_id=None,
            details=None,
            ip_address="127.0.0.1",
            user_agent="Mozilla/5.0",
        )
        assert entry is not None

    @pytest.mark.asyncio
    async def test_log_action_with_project_scope(self, audit_service, audit_repo):
        actor_id = uuid4()
        project_id = uuid4()
        await audit_service.log_action(
            actor_id=actor_id,
            action=AuditAction.PROJECT_CREATE,
            scope_type=ScopeType.PROJECT,
            scope_id=project_id,
            target_type="project",
            target_id=str(project_id),
            details={"name": "my project"},
        )
        audit_repo.log.assert_called_once()
        call_kwargs = audit_repo.log.call_args.kwargs
        assert call_kwargs["scope_id"] == project_id
        assert call_kwargs["target_type"] == "project"
        assert call_kwargs["details"] == {"name": "my project"}


class TestAuditServiceQuery:
    @pytest.mark.asyncio
    async def test_query_no_filters(self, audit_service, audit_repo):
        await audit_service.query()
        audit_repo.query.assert_called_once_with(
            actor_id=None,
            action=None,
            scope_type=None,
            scope_id=None,
            target_type=None,
            target_id=None,
            from_date=None,
            to_date=None,
            limit=50,
            offset=0,
        )

    @pytest.mark.asyncio
    async def test_query_with_filters(self, audit_service, audit_repo):
        actor_id = uuid4()
        from datetime import datetime, timezone
        from_date = datetime(2024, 1, 1, tzinfo=timezone.utc)
        await audit_service.query(
            actor_id=actor_id,
            action=AuditAction.LOGIN,
            from_date=from_date,
            limit=10,
            offset=20,
        )
        audit_repo.query.assert_called_once()
        call_kwargs = audit_repo.query.call_args.kwargs
        assert call_kwargs["actor_id"] == actor_id
        assert call_kwargs["action"] == AuditAction.LOGIN
        assert call_kwargs["from_date"] == from_date
        assert call_kwargs["limit"] == 10
        assert call_kwargs["offset"] == 20

    @pytest.mark.asyncio
    async def test_query_returns_entries(self, audit_service, audit_repo):
        entries = [_make_entry(action=AuditAction.LOGIN), _make_entry(action=AuditAction.LOGOUT)]
        audit_repo.query.return_value = entries
        result = await audit_service.query()
        assert len(result) == 2
        assert result[0].action == AuditAction.LOGIN
        assert result[1].action == AuditAction.LOGOUT
