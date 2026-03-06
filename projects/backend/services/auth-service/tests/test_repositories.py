"""Unit tests for auth_service.repositories module."""
from __future__ import annotations

from datetime import datetime, timezone
from unittest.mock import AsyncMock, MagicMock, patch
from uuid import uuid4

import pytest

from auth_service.core.exceptions import NotFoundError
from auth_service.domain.models import Project, ProjectMember, User
from auth_service.repositories.base import BaseRepository
from auth_service.repositories.projects import ProjectRepository
from auth_service.repositories.users import UserRepository


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture
def mock_pool_with_conn():
    """Create mock pool with proper async context manager for acquire()."""
    mock_pool = AsyncMock()
    mock_conn = AsyncMock()
    mock_cm = MagicMock()
    mock_cm.__aenter__ = AsyncMock(return_value=mock_conn)
    mock_cm.__aexit__ = AsyncMock(return_value=None)
    mock_pool.acquire = MagicMock(return_value=mock_cm)
    return mock_pool, mock_conn


# ---------------------------------------------------------------------------
# BaseRepository Tests
# ---------------------------------------------------------------------------

class TestBaseRepository:
    """Tests for BaseRepository helper class."""

    def test_init_with_pool(self):
        """Test BaseRepository initialization with pool."""
        mock_pool = AsyncMock()
        repo = BaseRepository(mock_pool)
        assert repo._pool is mock_pool

    @pytest.mark.asyncio
    async def test_fetchrow_returns_row(self, mock_pool_with_conn):
        """Test _fetchrow returns a row."""
        mock_pool, mock_conn = mock_pool_with_conn
        mock_conn.fetchrow = AsyncMock(return_value={"id": 1, "name": "test"})

        repo = BaseRepository(mock_pool)
        result = await repo._fetchrow("SELECT * FROM test WHERE id = $1", 1)

        assert result == {"id": 1, "name": "test"}
        mock_conn.fetchrow.assert_called_once_with("SELECT * FROM test WHERE id = $1", 1)

    @pytest.mark.asyncio
    async def test_fetchrow_returns_none(self, mock_pool_with_conn):
        """Test _fetchrow returns None when no row found."""
        mock_pool, mock_conn = mock_pool_with_conn
        mock_conn.fetchrow = AsyncMock(return_value=None)

        repo = BaseRepository(mock_pool)
        result = await repo._fetchrow("SELECT * FROM test WHERE id = $1", 999)

        assert result is None

    @pytest.mark.asyncio
    async def test_fetch_returns_rows(self, mock_pool_with_conn):
        """Test _fetch returns list of rows."""
        mock_pool, mock_conn = mock_pool_with_conn
        mock_conn.fetch = AsyncMock(return_value=[
            {"id": 1, "name": "test1"},
            {"id": 2, "name": "test2"},
        ])

        repo = BaseRepository(mock_pool)
        result = await repo._fetch("SELECT * FROM test")

        assert len(result) == 2
        assert result[0]["id"] == 1
        mock_conn.fetch.assert_called_once_with("SELECT * FROM test")

    @pytest.mark.asyncio
    async def test_fetch_returns_empty_list(self, mock_pool_with_conn):
        """Test _fetch returns empty list when no rows found."""
        mock_pool, mock_conn = mock_pool_with_conn
        mock_conn.fetch = AsyncMock(return_value=[])

        repo = BaseRepository(mock_pool)
        result = await repo._fetch("SELECT * FROM test WHERE id = $1", 999)

        assert result == []

    @pytest.mark.asyncio
    async def test_execute_returns_result(self, mock_pool_with_conn):
        """Test _execute returns result string."""
        mock_pool, mock_conn = mock_pool_with_conn
        mock_conn.execute = AsyncMock(return_value="DELETE 1")

        repo = BaseRepository(mock_pool)
        result = await repo._execute("DELETE FROM test WHERE id = $1", 1)

        assert result == "DELETE 1"
        mock_conn.execute.assert_called_once_with("DELETE FROM test WHERE id = $1", 1)

    @pytest.mark.asyncio
    async def test_fetchrow_with_multiple_params(self, mock_pool_with_conn):
        """Test _fetchrow with multiple parameters."""
        mock_pool, mock_conn = mock_pool_with_conn
        mock_conn.fetchrow = AsyncMock(return_value={"id": 1})

        repo = BaseRepository(mock_pool)
        await repo._fetchrow("SELECT * FROM test WHERE a = $1 AND b = $2", 1, "test")

        mock_conn.fetchrow.assert_called_once_with(
            "SELECT * FROM test WHERE a = $1 AND b = $2",
            1,
            "test",
        )

    @pytest.mark.asyncio
    async def test_fetch_with_multiple_params(self, mock_pool_with_conn):
        """Test _fetch with multiple parameters."""
        mock_pool, mock_conn = mock_pool_with_conn
        mock_conn.fetch = AsyncMock(return_value=[{"id": 1}])

        repo = BaseRepository(mock_pool)
        await repo._fetch("SELECT * FROM test WHERE a = $1 AND b = $2", 1, "test")

        mock_conn.fetch.assert_called_once_with(
            "SELECT * FROM test WHERE a = $1 AND b = $2",
            1,
            "test",
        )


# ---------------------------------------------------------------------------
# UserRepository Tests
# ---------------------------------------------------------------------------

class TestUserRepositoryCreate:
    """Tests for UserRepository.create method."""

    @pytest.mark.asyncio
    async def test_create_user_success(self):
        """Test create user returns User object."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn

        mock_row = {
            "id": uuid4(),
            "username": "testuser",
            "email": "test@example.com",
            "hashed_password": "hashed123",
            "password_change_required": False,
            "is_admin": False,
            "is_active": True,
            "created_at": datetime.now(timezone.utc),
            "updated_at": datetime.now(timezone.utc),
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        repo = UserRepository(mock_pool)
        user = await repo.create(
            username="testuser",
            email="test@example.com",
            hashed_password="hashed123",
        )

        assert isinstance(user, User)
        assert user.username == "testuser"
        assert user.email == "test@example.com"

    @pytest.mark.asyncio
    async def test_create_user_with_password_change_required(self):
        """Test create user with password_change_required=True."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_row = {
            "id": uuid4(),
            "username": "testuser",
            "email": "test@example.com",
            "hashed_password": "hashed123",
            "password_change_required": True,
            "is_admin": False,
            "is_active": True,
            "created_at": datetime.now(timezone.utc),
            "updated_at": datetime.now(timezone.utc),
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        repo = UserRepository(mock_pool)
        user = await repo.create(
            username="testuser",
            email="test@example.com",
            hashed_password="hashed123",
            password_change_required=True,
        )

        assert user.password_change_required is True

    @pytest.mark.asyncio
    async def test_create_user_raises_on_failure(self):
        """Test create raises RuntimeError on failure."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetchrow = AsyncMock(return_value=None)

        repo = UserRepository(mock_pool)

        with pytest.raises(RuntimeError, match="Failed to create user"):
            await repo.create(
                username="testuser",
                email="test@example.com",
                hashed_password="hashed123",
            )


class TestUserRepositoryGetters:
    """Tests for UserRepository get methods."""

    @pytest.mark.asyncio
    async def test_get_by_id_found(self):
        """Test get_by_id returns User when found."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn

        user_id = uuid4()
        mock_row = {
            "id": user_id,
            "username": "testuser",
            "email": "test@example.com",
            "hashed_password": "hashed123",
            "password_change_required": False,
            "is_admin": False,
            "is_active": True,
            "created_at": datetime.now(timezone.utc),
            "updated_at": datetime.now(timezone.utc),
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        repo = UserRepository(mock_pool)
        user = await repo.get_by_id(user_id)

        assert isinstance(user, User)
        assert user.id == user_id

    @pytest.mark.asyncio
    async def test_get_by_id_not_found(self):
        """Test get_by_id returns None when not found."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetchrow = AsyncMock(return_value=None)

        repo = UserRepository(mock_pool)
        user = await repo.get_by_id(uuid4())

        assert user is None

    @pytest.mark.asyncio
    async def test_get_by_username_found(self):
        """Test get_by_username returns User when found."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_row = {
            "id": uuid4(),
            "username": "testuser",
            "email": "test@example.com",
            "hashed_password": "hashed123",
            "password_change_required": False,
            "is_admin": False,
            "is_active": True,
            "created_at": datetime.now(timezone.utc),
            "updated_at": datetime.now(timezone.utc),
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        repo = UserRepository(mock_pool)
        user = await repo.get_by_username("testuser")

        assert isinstance(user, User)
        assert user.username == "testuser"

    @pytest.mark.asyncio
    async def test_get_by_username_not_found(self):
        """Test get_by_username returns None when not found."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetchrow = AsyncMock(return_value=None)

        repo = UserRepository(mock_pool)
        user = await repo.get_by_username("nonexistent")

        assert user is None

    @pytest.mark.asyncio
    async def test_get_by_email_found(self):
        """Test get_by_email returns User when found."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_row = {
            "id": uuid4(),
            "username": "testuser",
            "email": "test@example.com",
            "hashed_password": "hashed123",
            "password_change_required": False,
            "is_admin": False,
            "is_active": True,
            "created_at": datetime.now(timezone.utc),
            "updated_at": datetime.now(timezone.utc),
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        repo = UserRepository(mock_pool)
        user = await repo.get_by_email("test@example.com")

        assert isinstance(user, User)
        assert user.email == "test@example.com"

    @pytest.mark.asyncio
    async def test_get_by_email_not_found(self):
        """Test get_by_email returns None when not found."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetchrow = AsyncMock(return_value=None)

        repo = UserRepository(mock_pool)
        user = await repo.get_by_email("nonexistent@example.com")

        assert user is None


class TestUserRepositoryUpdate:
    """Tests for UserRepository update methods."""

    @pytest.mark.asyncio
    async def test_update_password_success(self):
        """Test update_password returns updated User."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn

        user_id = uuid4()
        mock_row = {
            "id": user_id,
            "username": "testuser",
            "email": "test@example.com",
            "hashed_password": "newhash123",
            "password_change_required": False,
            "is_admin": False,
            "is_active": True,
            "created_at": datetime.now(timezone.utc),
            "updated_at": datetime.now(timezone.utc),
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        repo = UserRepository(mock_pool)
        user = await repo.update_password(user_id, "newhash123")

        assert isinstance(user, User)
        assert user.hashed_password == "newhash123"

    @pytest.mark.asyncio
    async def test_update_password_with_change_required(self):
        """Test update_password with password_change_required."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn

        user_id = uuid4()
        mock_row = {
            "id": user_id,
            "username": "testuser",
            "email": "test@example.com",
            "hashed_password": "newhash123",
            "password_change_required": True,
            "is_admin": False,
            "is_active": True,
            "created_at": datetime.now(timezone.utc),
            "updated_at": datetime.now(timezone.utc),
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        repo = UserRepository(mock_pool)
        user = await repo.update_password(user_id, "newhash123", password_change_required=True)

        assert user.password_change_required is True

    @pytest.mark.asyncio
    async def test_update_password_raises_on_failure(self):
        """Test update_password raises on failure."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetchrow = AsyncMock(return_value=None)

        repo = UserRepository(mock_pool)

        with pytest.raises(RuntimeError, match="Failed to update password"):
            await repo.update_password(uuid4(), "newhash123")

    @pytest.mark.asyncio
    async def test_set_active_success(self):
        """Test set_active returns updated User."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn

        user_id = uuid4()
        mock_row = {
            "id": user_id,
            "username": "testuser",
            "email": "test@example.com",
            "hashed_password": "hashed123",
            "password_change_required": False,
            "is_admin": False,
            "is_active": False,
            "created_at": datetime.now(timezone.utc),
            "updated_at": datetime.now(timezone.utc),
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        repo = UserRepository(mock_pool)
        user = await repo.set_active(user_id, False)

        assert user.is_active is False

    @pytest.mark.asyncio
    async def test_set_active_raises_on_not_found(self):
        """Test set_active raises on user not found."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetchrow = AsyncMock(return_value=None)

        repo = UserRepository(mock_pool)

        with pytest.raises(RuntimeError, match="User not found"):
            await repo.set_active(uuid4(), False)

    @pytest.mark.asyncio
    async def test_set_admin_success(self):
        """Test set_admin returns updated User."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn

        user_id = uuid4()
        mock_row = {
            "id": user_id,
            "username": "testuser",
            "email": "test@example.com",
            "hashed_password": "hashed123",
            "password_change_required": False,
            "is_admin": True,
            "is_active": True,
            "created_at": datetime.now(timezone.utc),
            "updated_at": datetime.now(timezone.utc),
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        repo = UserRepository(mock_pool)
        user = await repo.set_admin(user_id, True)

        assert user.is_admin is True

    @pytest.mark.asyncio
    async def test_set_admin_raises_on_not_found(self):
        """Test set_admin raises on user not found."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetchrow = AsyncMock(return_value=None)

        repo = UserRepository(mock_pool)

        with pytest.raises(RuntimeError, match="User not found"):
            await repo.set_admin(uuid4(), True)


class TestUserRepositoryQueries:
    """Tests for UserRepository query methods."""

    @pytest.mark.asyncio
    async def test_user_exists_true(self):
        """Test user_exists returns True when user exists."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetchrow = AsyncMock(return_value={"exists": True})

        repo = UserRepository(mock_pool)
        exists = await repo.user_exists("testuser", "test@example.com")

        assert exists is True

    @pytest.mark.asyncio
    async def test_user_exists_false(self):
        """Test user_exists returns False when user doesn't exist."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetchrow = AsyncMock(return_value={"exists": False})

        repo = UserRepository(mock_pool)
        exists = await repo.user_exists("testuser", "test@example.com")

        assert exists is False

    @pytest.mark.asyncio
    async def test_user_exists_handles_none(self):
        """Test user_exists handles None response."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetchrow = AsyncMock(return_value=None)

        repo = UserRepository(mock_pool)
        exists = await repo.user_exists("testuser", "test@example.com")

        assert exists is False

    @pytest.mark.asyncio
    async def test_list_all_without_search(self):
        """Test list_all returns all users."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetch = AsyncMock(return_value=[
            {
                "id": uuid4(),
                "username": "user1",
                "email": "user1@example.com",
                "hashed_password": "hash1",
                "password_change_required": False,
                "is_admin": False,
                "is_active": True,
                "created_at": datetime.now(timezone.utc),
                "updated_at": datetime.now(timezone.utc),
            },
            {
                "id": uuid4(),
                "username": "user2",
                "email": "user2@example.com",
                "hashed_password": "hash2",
                "password_change_required": False,
                "is_admin": False,
                "is_active": True,
                "created_at": datetime.now(timezone.utc),
                "updated_at": datetime.now(timezone.utc),
            },
        ])

        repo = UserRepository(mock_pool)
        users = await repo.list_all()

        assert len(users) == 2
        assert all(isinstance(u, User) for u in users)

    @pytest.mark.asyncio
    async def test_list_all_with_search(self):
        """Test list_all with search filter."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetch = AsyncMock(return_value=[
            {
                "id": uuid4(),
                "username": "testuser",
                "email": "test@example.com",
                "hashed_password": "hash1",
                "password_change_required": False,
                "is_admin": False,
                "is_active": True,
                "created_at": datetime.now(timezone.utc),
                "updated_at": datetime.now(timezone.utc),
            },
        ])

        repo = UserRepository(mock_pool)
        users = await repo.list_all(search="test")

        assert len(users) == 1
        assert users[0].username == "testuser"

    @pytest.mark.asyncio
    async def test_list_all_empty(self):
        """Test list_all returns empty list."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetch = AsyncMock(return_value=[])

        repo = UserRepository(mock_pool)
        users = await repo.list_all()

        assert users == []

    @pytest.mark.asyncio
    async def test_count_admins(self):
        """Test count_admins returns count."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetchrow = AsyncMock(return_value={"count": 3})

        repo = UserRepository(mock_pool)
        count = await repo.count_admins()

        assert count == 3

    @pytest.mark.asyncio
    async def test_count_admins_zero(self):
        """Test count_admins returns 0."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetchrow = AsyncMock(return_value={"count": 0})

        repo = UserRepository(mock_pool)
        count = await repo.count_admins()

        assert count == 0

    @pytest.mark.asyncio
    async def test_delete_success(self):
        """Test delete returns True on success."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.execute = AsyncMock(return_value="DELETE 1")

        repo = UserRepository(mock_pool)
        result = await repo.delete(uuid4())

        assert result is True

    @pytest.mark.asyncio
    async def test_delete_not_found(self):
        """Test delete returns False when not found."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.execute = AsyncMock(return_value="DELETE 0")

        repo = UserRepository(mock_pool)
        result = await repo.delete(uuid4())

        assert result is False


# ---------------------------------------------------------------------------
# ProjectRepository Tests
# ---------------------------------------------------------------------------

class TestProjectRepositoryCreate:
    """Tests for ProjectRepository.create method."""

    @pytest.mark.asyncio
    async def test_create_project_success(self):
        """Test create project returns Project object."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn

        owner_id = uuid4()
        mock_row = {
            "id": uuid4(),
            "name": "Test Project",
            "description": "Test description",
            "owner_id": owner_id,
            "created_at": datetime.now(timezone.utc),
            "updated_at": datetime.now(timezone.utc),
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        repo = ProjectRepository(mock_pool)
        project = await repo.create(
            name="Test Project",
            description="Test description",
            owner_id=owner_id,
        )

        assert isinstance(project, Project)
        assert project.name == "Test Project"
        assert project.description == "Test description"

    @pytest.mark.asyncio
    async def test_create_project_without_description(self):
        """Test create project with None description."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn

        owner_id = uuid4()
        mock_row = {
            "id": uuid4(),
            "name": "Test Project",
            "description": None,
            "owner_id": owner_id,
            "created_at": datetime.now(timezone.utc),
            "updated_at": datetime.now(timezone.utc),
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        repo = ProjectRepository(mock_pool)
        project = await repo.create(
            name="Test Project",
            description=None,
            owner_id=owner_id,
        )

        assert project.description is None

    @pytest.mark.asyncio
    async def test_create_project_raises_on_failure(self):
        """Test create raises RuntimeError on failure."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetchrow = AsyncMock(return_value=None)

        repo = ProjectRepository(mock_pool)

        with pytest.raises(RuntimeError, match="Failed to create project"):
            await repo.create(
                name="Test Project",
                description=None,
                owner_id=uuid4(),
            )


class TestProjectRepositoryGetters:
    """Tests for ProjectRepository get methods."""

    @pytest.mark.asyncio
    async def test_get_by_id_found(self):
        """Test get_by_id returns Project when found."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn

        project_id = uuid4()
        mock_row = {
            "id": project_id,
            "name": "Test Project",
            "description": "Test description",
            "owner_id": uuid4(),
            "created_at": datetime.now(timezone.utc),
            "updated_at": datetime.now(timezone.utc),
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        repo = ProjectRepository(mock_pool)
        project = await repo.get_by_id(project_id)

        assert isinstance(project, Project)
        assert project.id == project_id

    @pytest.mark.asyncio
    async def test_get_by_id_not_found(self):
        """Test get_by_id returns None when not found."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetchrow = AsyncMock(return_value=None)

        repo = ProjectRepository(mock_pool)
        project = await repo.get_by_id(uuid4())

        assert project is None

    @pytest.mark.asyncio
    async def test_get_by_id_or_raise_found(self):
        """Test get_by_id_or_raise returns Project when found."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn

        project_id = uuid4()
        mock_row = {
            "id": project_id,
            "name": "Test Project",
            "description": None,
            "owner_id": uuid4(),
            "created_at": datetime.now(timezone.utc),
            "updated_at": datetime.now(timezone.utc),
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        repo = ProjectRepository(mock_pool)
        project = await repo.get_by_id_or_raise(project_id)

        assert isinstance(project, Project)

    @pytest.mark.asyncio
    async def test_get_by_id_or_raise_not_found(self):
        """Test get_by_id_or_raise raises NotFoundError."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetchrow = AsyncMock(return_value=None)

        repo = ProjectRepository(mock_pool)

        with pytest.raises(NotFoundError, match="Project"):
            await repo.get_by_id_or_raise(uuid4())

    @pytest.mark.asyncio
    async def test_list_by_user(self):
        """Test list_by_user returns user's projects."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn

        user_id = uuid4()
        mock_conn.fetch = AsyncMock(return_value=[
            {
                "id": uuid4(),
                "name": "Project 1",
                "description": None,
                "owner_id": uuid4(),
                "created_at": datetime.now(timezone.utc),
                "updated_at": datetime.now(timezone.utc),
            },
            {
                "id": uuid4(),
                "name": "Project 2",
                "description": "Desc 2",
                "owner_id": uuid4(),
                "created_at": datetime.now(timezone.utc),
                "updated_at": datetime.now(timezone.utc),
            },
        ])

        repo = ProjectRepository(mock_pool)
        projects = await repo.list_by_user(user_id)

        assert len(projects) == 2
        assert all(isinstance(p, Project) for p in projects)

    @pytest.mark.asyncio
    async def test_list_by_user_empty(self):
        """Test list_by_user returns empty list."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetch = AsyncMock(return_value=[])

        repo = ProjectRepository(mock_pool)
        projects = await repo.list_by_user(uuid4())

        assert projects == []

    @pytest.mark.asyncio
    async def test_list_by_owner(self):
        """Test list_by_owner returns owned projects."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn

        owner_id = uuid4()
        mock_conn.fetch = AsyncMock(return_value=[
            {
                "id": uuid4(),
                "name": "Owned Project",
                "description": None,
                "owner_id": owner_id,
                "created_at": datetime.now(timezone.utc),
                "updated_at": datetime.now(timezone.utc),
            },
        ])

        repo = ProjectRepository(mock_pool)
        projects = await repo.list_by_owner(owner_id)

        assert len(projects) == 1
        assert projects[0].owner_id == owner_id


class TestProjectRepositoryUpdate:
    """Tests for ProjectRepository.update method."""

    @pytest.mark.asyncio
    async def test_update_name(self):
        """Test update project name."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn

        project_id = uuid4()
        mock_row = {
            "id": project_id,
            "name": "Updated Name",
            "description": None,
            "owner_id": uuid4(),
            "created_at": datetime.now(timezone.utc),
            "updated_at": datetime.now(timezone.utc),
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        repo = ProjectRepository(mock_pool)
        project = await repo.update(project_id, name="Updated Name")

        assert project.name == "Updated Name"

    @pytest.mark.asyncio
    async def test_update_description(self):
        """Test update project description."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn

        project_id = uuid4()
        mock_row = {
            "id": project_id,
            "name": "Test Project",
            "description": "Updated description",
            "owner_id": uuid4(),
            "created_at": datetime.now(timezone.utc),
            "updated_at": datetime.now(timezone.utc),
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        repo = ProjectRepository(mock_pool)
        project = await repo.update(project_id, description="Updated description")

        assert project.description == "Updated description"

    @pytest.mark.asyncio
    async def test_update_no_fields(self):
        """Test update with no fields returns existing project."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn

        project_id = uuid4()
        mock_row = {
            "id": project_id,
            "name": "Test Project",
            "description": None,
            "owner_id": uuid4(),
            "created_at": datetime.now(timezone.utc),
            "updated_at": datetime.now(timezone.utc),
        }

        async def fetchrow_side_effect(*args, **kwargs):
            if "UPDATE" in str(args[0]):
                return None
            return mock_row

        mock_conn.fetchrow = AsyncMock(side_effect=fetchrow_side_effect)

        repo = ProjectRepository(mock_pool)
        project = await repo.update(project_id)

        assert project.name == "Test Project"

    @pytest.mark.asyncio
    async def test_update_not_found(self):
        """Test update raises NotFoundError."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetchrow = AsyncMock(return_value=None)

        repo = ProjectRepository(mock_pool)

        with pytest.raises(NotFoundError, match="Project"):
            await repo.update(uuid4(), name="New Name")


class TestProjectRepositoryMembers:
    """Tests for ProjectRepository member methods."""

    @pytest.mark.asyncio
    async def test_is_member_true(self):
        """Test is_member returns True."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetchrow = AsyncMock(return_value={"exists": True})

        repo = ProjectRepository(mock_pool)
        result = await repo.is_member(uuid4(), uuid4())

        assert result is True

    @pytest.mark.asyncio
    async def test_is_member_false(self):
        """Test is_member returns False."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetchrow = AsyncMock(return_value={"exists": False})

        repo = ProjectRepository(mock_pool)
        result = await repo.is_member(uuid4(), uuid4())

        assert result is False

    @pytest.mark.asyncio
    async def test_get_member_role_found(self):
        """Test get_member_role returns role."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetchrow = AsyncMock(return_value={"role": "editor"})

        repo = ProjectRepository(mock_pool)
        role = await repo.get_member_role(uuid4(), uuid4())

        assert role == "editor"

    @pytest.mark.asyncio
    async def test_get_member_role_not_found(self):
        """Test get_member_role returns None."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetchrow = AsyncMock(return_value=None)

        repo = ProjectRepository(mock_pool)
        role = await repo.get_member_role(uuid4(), uuid4())

        assert role is None

    @pytest.mark.asyncio
    async def test_add_member_success(self):
        """Test add_member returns ProjectMember."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn

        mock_row = {
            "project_id": uuid4(),
            "user_id": uuid4(),
            "role": "editor",
            "created_at": datetime.now(timezone.utc),
        }
        mock_conn.fetchrow = AsyncMock(return_value=mock_row)

        repo = ProjectRepository(mock_pool)
        member = await repo.add_member(uuid4(), uuid4(), "editor")

        assert isinstance(member, ProjectMember)
        assert member.role == "editor"

    @pytest.mark.asyncio
    async def test_add_member_raises_on_failure(self):
        """Test add_member raises on failure."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetchrow = AsyncMock(return_value=None)

        repo = ProjectRepository(mock_pool)

        with pytest.raises(RuntimeError, match="Failed to add project member"):
            await repo.add_member(uuid4(), uuid4(), "editor")

    @pytest.mark.asyncio
    async def test_remove_member_success(self):
        """Test remove_member succeeds."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.execute = AsyncMock(return_value="DELETE 1")

        repo = ProjectRepository(mock_pool)
        # Should not raise
        await repo.remove_member(uuid4(), uuid4())

    @pytest.mark.asyncio
    async def test_remove_member_not_found(self):
        """Test remove_member raises NotFoundError."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.execute = AsyncMock(return_value="DELETE 0")

        repo = ProjectRepository(mock_pool)

        with pytest.raises(NotFoundError, match="Member"):
            await repo.remove_member(uuid4(), uuid4())

    @pytest.mark.asyncio
    async def test_list_members(self):
        """Test list_members returns members with usernames."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.fetch = AsyncMock(return_value=[
            {
                "project_id": uuid4(),
                "user_id": uuid4(),
                "role": "owner",
                "created_at": datetime.now(timezone.utc),
                "username": "owner_user",
            },
            {
                "project_id": uuid4(),
                "user_id": uuid4(),
                "role": "editor",
                "created_at": datetime.now(timezone.utc),
                "username": "editor_user",
            },
        ])

        repo = ProjectRepository(mock_pool)
        members = await repo.list_members(uuid4())

        assert len(members) == 2
        assert members[0]["username"] == "owner_user"
        assert members[1]["role"] == "editor"


class TestProjectRepositoryDelete:
    """Tests for ProjectRepository.delete method."""

    @pytest.mark.asyncio
    async def test_delete_success(self):
        """Test delete succeeds."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.execute = AsyncMock(return_value="DELETE 1")

        repo = ProjectRepository(mock_pool)
        # Should not raise
        await repo.delete(uuid4())

    @pytest.mark.asyncio
    async def test_delete_not_found(self):
        """Test delete raises NotFoundError."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_conn.execute = AsyncMock(return_value="DELETE 0")

        repo = ProjectRepository(mock_pool)

        with pytest.raises(NotFoundError, match="Project"):
            await repo.delete(uuid4())
