"""Unit tests for backend_common.db.pool module."""
from __future__ import annotations

from unittest.mock import AsyncMock, MagicMock, patch

import asyncpg
import pytest

from backend_common.db import pool


class TestPoolInitialization:
    """Tests for pool initialization."""

    @pytest.mark.asyncio
    async def test_init_pool_creates_pool(self):
        """Test that init_pool creates an asyncpg pool."""
        with patch("backend_common.db.pool.asyncpg.create_pool") as mock_create:
            mock_pool = AsyncMock()
            mock_create.return_value = mock_pool

            await pool.init_pool("postgresql://localhost/test", 10)

            mock_create.assert_called_once_with(
                dsn="postgresql://localhost/test",
                max_size=10,
            )
            assert pool.pool is mock_pool
            assert pool._sync_pool is mock_pool

    @pytest.mark.asyncio
    async def test_init_pool_sets_global_pool(self):
        """Test that pool is set as global."""
        # Reset global state
        pool.pool = None
        pool._sync_pool = None

        with patch("backend_common.db.pool.asyncpg.create_pool") as mock_create:
            mock_pool = AsyncMock()
            mock_create.return_value = mock_pool

            await pool.init_pool("postgresql://localhost/db", 5)

            assert pool.pool is not None
            assert pool._sync_pool is not None

    @pytest.mark.asyncio
    async def test_init_pool_doesnt_recreate_if_exists(self):
        """Test that init_pool doesn't recreate existing pool."""
        # Set existing pool
        existing_pool = AsyncMock()
        pool.pool = existing_pool
        pool._sync_pool = existing_pool

        with patch("backend_common.db.pool.asyncpg.create_pool") as mock_create:
            await pool.init_pool("postgresql://localhost/db", 5)

            mock_create.assert_not_called()
            assert pool.pool is existing_pool

    @pytest.mark.asyncio
    async def test_init_pool_with_app_parameter(self):
        """Test init_pool accepts optional app parameter."""
        with patch("backend_common.db.pool.asyncpg.create_pool") as mock_create:
            mock_pool = AsyncMock()
            mock_create.return_value = mock_pool

            mock_app = MagicMock()
            await pool.init_pool("postgresql://localhost/db", 10, mock_app)

            mock_create.assert_called_once()

    @pytest.mark.asyncio
    async def test_init_pool_resets_sync_pool(self):
        """Test that init_pool resets _sync_pool."""
        pool.pool = None
        pool._sync_pool = AsyncMock()

        with patch("backend_common.db.pool.asyncpg.create_pool") as mock_create:
            new_pool = AsyncMock()
            mock_create.return_value = new_pool

            await pool.init_pool("postgresql://localhost/db", 10)

            assert pool._sync_pool is new_pool


class TestPoolClose:
    """Tests for pool closing."""

    @pytest.mark.asyncio
    async def test_close_pool_closes_pool(self):
        """Test that close_pool closes the pool."""
        mock_pool = AsyncMock()
        pool.pool = mock_pool
        pool._sync_pool = mock_pool

        await pool.close_pool()

        mock_pool.close.assert_awaited_once()
        assert pool.pool is None
        assert pool._sync_pool is None

    @pytest.mark.asyncio
    async def test_close_pool_without_initialized_pool(self):
        """Test close_pool when pool is not initialized."""
        pool.pool = None
        pool._sync_pool = None

        # Should not raise
        await pool.close_pool()

        assert pool.pool is None
        assert pool._sync_pool is None

    @pytest.mark.asyncio
    async def test_close_pool_with_app_parameter(self):
        """Test close_pool accepts optional app parameter."""
        mock_pool = AsyncMock()
        pool.pool = mock_pool
        pool._sync_pool = mock_pool

        mock_app = MagicMock()
        await pool.close_pool(mock_app)

        mock_pool.close.assert_awaited_once()


class TestGetPool:
    """Tests for get_pool function."""

    @pytest.mark.asyncio
    async def test_get_pool_returns_pool(self):
        """Test get_pool returns the initialized pool."""
        mock_pool = AsyncMock()
        pool.pool = mock_pool

        result = await pool.get_pool()

        assert result is mock_pool

    @pytest.mark.asyncio
    async def test_get_pool_raises_if_not_initialized(self):
        """Test get_pool raises RuntimeError if pool not initialized."""
        pool.pool = None

        with pytest.raises(RuntimeError, match="Database pool not initialized"):
            await pool.get_pool()

    @pytest.mark.asyncio
    async def test_get_pool_creates_if_none(self):
        """Test get_pool behavior when pool is None."""
        pool.pool = None

        with pytest.raises(RuntimeError):
            await pool.get_pool()


class TestGetPoolSync:
    """Tests for get_pool_sync function."""

    def test_get_pool_sync_returns_pool(self):
        """Test get_pool_sync returns the initialized pool."""
        mock_pool = AsyncMock()
        pool._sync_pool = mock_pool

        result = pool.get_pool_sync()

        assert result is mock_pool

    def test_get_pool_sync_raises_if_not_initialized(self):
        """Test get_pool_sync raises RuntimeError if pool not initialized."""
        pool._sync_pool = None

        with pytest.raises(RuntimeError, match="Database pool not initialized"):
            pool.get_pool_sync()


class TestGetConnection:
    """Tests for get_connection context manager."""

    @pytest.mark.asyncio
    async def test_get_connection_yields_connection(self):
        """Test get_connection yields a connection from pool."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()

        # Setup async context manager
        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_pool.acquire.return_value.__aexit__.return_value = None

        pool.pool = mock_pool

        async with pool.get_connection() as conn:
            assert conn is mock_conn

        mock_pool.acquire.assert_called_once()

    @pytest.mark.asyncio
    async def test_get_connection_releases_on_exit(self):
        """Test get_connection releases connection on exit."""
        mock_pool = AsyncMock()
        mock_conn = AsyncMock()

        mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
        mock_pool.acquire.return_value.__aexit__ = AsyncMock()

        pool.pool = mock_pool

        async with pool.get_connection() as conn:
            pass

        mock_pool.acquire.return_value.__aexit__.assert_called_once()

    @pytest.mark.asyncio
    async def test_get_connection_raises_if_pool_not_initialized(self):
        """Test get_connection raises if pool not initialized."""
        pool.pool = None

        with pytest.raises(RuntimeError, match="Database pool not initialized"):
            async with pool.get_connection():
                pass


class TestCreatePoolWrappers:
    """Tests for create_pool_wrappers function."""

    def test_create_pool_wrappers_returns_functions(self):
        """Test create_pool_wrappers returns init and close functions."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/db"
        mock_settings.db_pool_size = 10

        init_fn, close_fn = pool.create_pool_wrappers(mock_settings)

        assert callable(init_fn)
        assert callable(close_fn)

    @pytest.mark.asyncio
    async def test_init_pool_wrapper_uses_settings(self):
        """Test init_pool_wrapper uses settings."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/testdb"
        mock_settings.db_pool_size = 15

        with patch("backend_common.db.pool.init_pool") as mock_init:
            init_fn, _ = pool.create_pool_wrappers(mock_settings)
            await init_fn()

            mock_init.assert_called_once_with(
                "postgresql://localhost/testdb",
                15,
                None,
            )

    @pytest.mark.asyncio
    async def test_init_pool_wrapper_with_app(self):
        """Test init_pool_wrapper passes app parameter."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/db"
        mock_settings.db_pool_size = 10
        mock_app = MagicMock()

        with patch("backend_common.db.pool.init_pool") as mock_init:
            init_fn, _ = pool.create_pool_wrappers(mock_settings)
            await init_fn(mock_app)

            mock_init.assert_called_once_with(
                "postgresql://localhost/db",
                10,
                mock_app,
            )

    @pytest.mark.asyncio
    async def test_close_pool_wrapper(self):
        """Test close_pool_wrapper calls close_pool."""
        mock_settings = MagicMock()

        with patch("backend_common.db.pool.close_pool") as mock_close:
            _, close_fn = pool.create_pool_wrappers(mock_settings)
            await close_fn()

            mock_close.assert_called_once_with(None)

    @pytest.mark.asyncio
    async def test_close_pool_wrapper_with_app(self):
        """Test close_pool_wrapper with app parameter."""
        mock_settings = MagicMock()
        mock_app = MagicMock()

        with patch("backend_common.db.pool.close_pool") as mock_close:
            _, close_fn = pool.create_pool_wrappers(mock_settings)
            await close_pool_wrapper(mock_app)

            mock_close.assert_called_once_with(mock_app)


class TestPoolServiceFunctions:
    """Tests for service compatibility functions."""

    @pytest.mark.asyncio
    async def test_init_pool_service_initializes_pool(self):
        """Test init_pool_service initializes pool."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/service_db"
        mock_settings.db_pool_size = 20

        with patch("backend_common.db.pool.init_pool") as mock_init:
            await pool.init_pool_service(settings=mock_settings)

            mock_init.assert_called_once_with(
                "postgresql://localhost/service_db",
                20,
                None,
            )

    @pytest.mark.asyncio
    async def test_init_pool_service_with_app(self):
        """Test init_pool_service with app parameter."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/db"
        mock_settings.db_pool_size = 10
        mock_app = MagicMock()

        with patch("backend_common.db.pool.init_pool") as mock_init:
            await pool.init_pool_service(mock_app, settings=mock_settings)

            mock_init.assert_called_once_with(
                "postgresql://localhost/db",
                10,
                mock_app,
            )

    @pytest.mark.asyncio
    async def test_init_pool_service_without_settings_raises(self):
        """Test init_pool_service raises without settings."""
        with pytest.raises(RuntimeError, match="settings parameter is required"):
            await pool.init_pool_service(settings=None)

    @pytest.mark.asyncio
    async def test_close_pool_service(self):
        """Test close_pool_service closes pool."""
        mock_pool_obj = AsyncMock()
        pool.pool = mock_pool_obj

        with patch("backend_common.db.pool.close_pool") as mock_close:
            await pool.close_pool_service()

            mock_close.assert_called_once()

    @pytest.mark.asyncio
    async def test_get_pool_service_async(self):
        """Test get_pool_service_async returns pool."""
        mock_pool_obj = AsyncMock()
        pool.pool = mock_pool_obj

        result = await pool.get_pool_service()

        assert result is mock_pool_obj

    def test_get_pool_service_sync(self):
        """Test get_pool_service_sync returns pool."""
        mock_pool_obj = AsyncMock()
        pool._sync_pool = mock_pool_obj

        result = pool.get_pool_service_sync()

        assert result is mock_pool_obj


class TestPoolGlobalState:
    """Tests for global pool state management."""

    @pytest.mark.asyncio
    async def test_pool_state_after_init_and_close(self):
        """Test global state is properly managed."""
        pool.pool = None
        pool._sync_pool = None

        with patch("backend_common.db.pool.asyncpg.create_pool") as mock_create:
            mock_pool_obj = AsyncMock()
            mock_create.return_value = mock_pool_obj

            # Initialize
            await pool.init_pool("postgresql://localhost/db", 10)
            assert pool.pool is mock_pool_obj
            assert pool._sync_pool is mock_pool_obj

            # Close
            await pool.close_pool()
            assert pool.pool is None
            assert pool._sync_pool is None

    @pytest.mark.asyncio
    async def test_multiple_init_calls_use_existing(self):
        """Test multiple init calls don't create multiple pools."""
        pool.pool = None
        pool._sync_pool = None

        with patch("backend_common.db.pool.asyncpg.create_pool") as mock_create:
            mock_pool1 = AsyncMock()
            mock_pool2 = AsyncMock()
            mock_create.side_effect = [mock_pool1, mock_pool2]

            # First init
            await pool.init_pool("postgresql://localhost/db1", 10)
            assert pool.pool is mock_pool1

            # Second init (should not create new pool)
            await pool.init_pool("postgresql://localhost/db2", 20)
            assert pool.pool is mock_pool1
            mock_create.assert_called_once()
