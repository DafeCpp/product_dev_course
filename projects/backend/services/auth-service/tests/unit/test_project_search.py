"""Unit tests for project search, role filter and pagination."""
from __future__ import annotations

from datetime import datetime, timezone
from unittest.mock import AsyncMock, MagicMock
from uuid import uuid4

import pytest

from auth_service.domain.models import Project
from auth_service.repositories.projects import ProjectRepository
from auth_service.services.projects import ProjectService


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_pool_with_conn(
    fetch_return: list | None = None,
    fetchrow_return: dict | None = None,
) -> tuple[AsyncMock, AsyncMock]:
    mock_pool = AsyncMock()
    mock_conn = AsyncMock()
    mock_cm = MagicMock()
    mock_cm.__aenter__ = AsyncMock(return_value=mock_conn)
    mock_cm.__aexit__ = AsyncMock(return_value=None)
    mock_pool.acquire = MagicMock(return_value=mock_cm)

    if fetch_return is not None:
        mock_conn.fetch = AsyncMock(return_value=fetch_return)
    if fetchrow_return is not None:
        mock_conn.fetchrow = AsyncMock(return_value=fetchrow_return)

    return mock_pool, mock_conn


def _project_row(name: str = "Project", owner_id=None) -> dict:
    return {
        "id": uuid4(),
        "name": name,
        "description": None,
        "owner_id": owner_id or uuid4(),
        "created_at": datetime.now(timezone.utc),
        "updated_at": datetime.now(timezone.utc),
    }


# ---------------------------------------------------------------------------
# Repository tests
# ---------------------------------------------------------------------------

class TestProjectRepositoryListByUser:
    """Tests for ProjectRepository.list_by_user with filters."""

    @pytest.mark.asyncio
    async def test_list_projects_no_filters(self):
        """list_by_user without filters returns all projects and total count."""
        rows = [_project_row("Alpha"), _project_row("Beta")]
        mock_pool, mock_conn = _make_pool_with_conn()
        # fetchrow → COUNT query; fetch → SELECT query
        mock_conn.fetchrow = AsyncMock(return_value={"count": 2})
        mock_conn.fetch = AsyncMock(return_value=rows)

        repo = ProjectRepository(mock_pool)
        projects, total = await repo.list_by_user(uuid4())

        assert total == 2
        assert len(projects) == 2
        assert all(isinstance(p, Project) for p in projects)

    @pytest.mark.asyncio
    async def test_list_projects_with_search(self):
        """list_by_user passes search term and returns filtered results."""
        rows = [_project_row("Aerodynamics")]
        mock_pool, mock_conn = _make_pool_with_conn()
        mock_conn.fetchrow = AsyncMock(return_value={"count": 1})
        mock_conn.fetch = AsyncMock(return_value=rows)

        repo = ProjectRepository(mock_pool)
        projects, total = await repo.list_by_user(uuid4(), search="aero")

        assert total == 1
        assert projects[0].name == "Aerodynamics"

        # Verify search parameter was forwarded to the DB call
        fetch_call_args = mock_conn.fetch.call_args
        assert "aero" in fetch_call_args.args

    @pytest.mark.asyncio
    async def test_list_projects_with_role_filter(self):
        """list_by_user passes role filter and returns only matching projects."""
        rows = [_project_row("Owned Project")]
        mock_pool, mock_conn = _make_pool_with_conn()
        mock_conn.fetchrow = AsyncMock(return_value={"count": 1})
        mock_conn.fetch = AsyncMock(return_value=rows)

        repo = ProjectRepository(mock_pool)
        projects, total = await repo.list_by_user(uuid4(), role="owner")

        assert total == 1
        assert projects[0].name == "Owned Project"

        fetch_call_args = mock_conn.fetch.call_args
        assert "owner" in fetch_call_args.args

    @pytest.mark.asyncio
    async def test_list_projects_pagination(self):
        """list_by_user forwards limit and offset to the query."""
        rows = [_project_row("Page Project")]
        mock_pool, mock_conn = _make_pool_with_conn()
        mock_conn.fetchrow = AsyncMock(return_value={"count": 42})
        mock_conn.fetch = AsyncMock(return_value=rows)

        repo = ProjectRepository(mock_pool)
        projects, total = await repo.list_by_user(uuid4(), limit=5, offset=10)

        assert total == 42
        assert len(projects) == 1

        fetch_call_args = mock_conn.fetch.call_args
        # limit=5, offset=10 must appear in call positional args
        assert 5 in fetch_call_args.args
        assert 10 in fetch_call_args.args

    @pytest.mark.asyncio
    async def test_list_projects_empty_result(self):
        """list_by_user returns empty list and zero total when no projects match."""
        mock_pool, mock_conn = _make_pool_with_conn()
        mock_conn.fetchrow = AsyncMock(return_value={"count": 0})
        mock_conn.fetch = AsyncMock(return_value=[])

        repo = ProjectRepository(mock_pool)
        projects, total = await repo.list_by_user(uuid4(), search="nonexistent")

        assert total == 0
        assert projects == []

    @pytest.mark.asyncio
    async def test_list_projects_search_and_role_combined(self):
        """list_by_user passes both search and role at the same time."""
        mock_pool, mock_conn = _make_pool_with_conn()
        mock_conn.fetchrow = AsyncMock(return_value={"count": 0})
        mock_conn.fetch = AsyncMock(return_value=[])

        repo = ProjectRepository(mock_pool)
        _, _ = await repo.list_by_user(uuid4(), search="wind", role="editor")

        fetch_call_args = mock_conn.fetch.call_args
        assert "wind" in fetch_call_args.args
        assert "editor" in fetch_call_args.args


# ---------------------------------------------------------------------------
# Service tests
# ---------------------------------------------------------------------------

class TestProjectServiceListUserProjects:
    """Tests for ProjectService.list_user_projects signature and delegation."""

    def _make_service(self, repo: ProjectRepository) -> ProjectService:
        return ProjectService(
            project_repo=repo,
            user_repo=MagicMock(),
            user_role_repo=MagicMock(),
            permission_service=MagicMock(),
        )

    @pytest.mark.asyncio
    async def test_list_user_projects_delegates_to_repo(self):
        """Service.list_user_projects delegates all params to the repository."""
        mock_pool, mock_conn = _make_pool_with_conn()
        mock_conn.fetchrow = AsyncMock(return_value={"count": 3})
        mock_conn.fetch = AsyncMock(return_value=[_project_row(), _project_row(), _project_row()])

        repo = ProjectRepository(mock_pool)
        service = self._make_service(repo)

        user_id = uuid4()
        projects, total = await service.list_user_projects(
            user_id, search="exp", role="viewer", limit=10, offset=5,
        )

        assert total == 3
        assert len(projects) == 3

        fetch_call_args = mock_conn.fetch.call_args
        assert user_id in fetch_call_args.args
        assert "exp" in fetch_call_args.args
        assert "viewer" in fetch_call_args.args
        assert 10 in fetch_call_args.args
        assert 5 in fetch_call_args.args

    @pytest.mark.asyncio
    async def test_list_user_projects_no_filters_backward_compat(self):
        """Service.list_user_projects works with no optional args (backward compat)."""
        mock_pool, mock_conn = _make_pool_with_conn()
        mock_conn.fetchrow = AsyncMock(return_value={"count": 1})
        mock_conn.fetch = AsyncMock(return_value=[_project_row()])

        repo = ProjectRepository(mock_pool)
        service = self._make_service(repo)

        projects, total = await service.list_user_projects(uuid4())

        assert total == 1
        assert len(projects) == 1
