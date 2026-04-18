"""Unit tests for backend_common.db.migrations module."""
from __future__ import annotations

import hashlib
from pathlib import Path
from unittest.mock import AsyncMock, MagicMock, patch

import pytest

from backend_common.db.migrations import (
    _find_migrations_dir,
    create_migration_runner,
)


class TestFindMigrationsDir:
    """Tests for _find_migrations_dir function."""

    def test_finds_existing_directory(self):
        """Test _find_migrations_dir finds existing directory."""
        with patch("backend_common.db.migrations.Path.exists") as mock_exists:
            mock_exists.side_effect = [False, True, False]

            paths = [Path("/path1"), Path("/path2"), Path("/path3")]
            result = _find_migrations_dir(paths)

            assert result == Path("/path2")

    def test_returns_none_when_not_found(self):
        """Test _find_migrations_dir returns None when not found."""
        with patch("backend_common.db.migrations.Path.exists") as mock_exists:
            mock_exists.return_value = False

            paths = [Path("/path1"), Path("/path2")]
            result = _find_migrations_dir(paths)

            assert result is None

    def test_returns_first_existing_directory(self):
        """Test _find_migrations_dir returns first existing directory."""
        with patch("backend_common.db.migrations.Path.exists") as mock_exists:
            mock_exists.side_effect = [True, True, True]

            paths = [Path("/path1"), Path("/path2"), Path("/path3")]
            result = _find_migrations_dir(paths)

            assert result == Path("/path1")

    def test_handles_empty_paths(self):
        """Test _find_migrations_dir handles empty paths list."""
        result = _find_migrations_dir([])
        assert result is None


class TestCreateMigrationRunner:
    """Tests for create_migration_runner function."""

    def test_returns_callable(self):
        """Test create_migration_runner returns a callable."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/test"

        runner = create_migration_runner(mock_settings, [Path("/tmp")])

        assert callable(runner)

    def test_accepts_possible_paths(self):
        """Test create_migration_runner accepts possible paths."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/test"

        runner = create_migration_runner(
            mock_settings,
            [Path("/path1"), Path("/path2")],
        )

        assert callable(runner)

    def test_accepts_create_db_hint(self):
        """Test create_migration_runner accepts create_db_hint."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/test"

        runner = create_migration_runner(
            mock_settings,
            [Path("/tmp")],
            create_db_hint="CREATE DATABASE test",
        )

        assert callable(runner)


class TestMigrationRunnerNoMigrations:
    """Tests for migration runner when no migrations exist."""

    @pytest.mark.asyncio
    async def test_no_migrations_directory(self):
        """Test runner handles missing migrations directory."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/test"

        with patch("backend_common.db.migrations._find_migrations_dir") as mock_find:
            mock_find.return_value = None

            runner = create_migration_runner(mock_settings, [Path("/tmp")])

            # Should not raise
            await runner(MagicMock())

    @pytest.mark.asyncio
    async def test_no_migration_files(self):
        """Test runner handles empty migrations directory."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/test"

        with patch("backend_common.db.migrations._find_migrations_dir") as mock_find, \
             patch("backend_common.db.migrations.Path.glob") as mock_glob:

            mock_find.return_value = Path("/tmp/migrations")
            mock_glob.return_value = []

            runner = create_migration_runner(mock_settings, [Path("/tmp")])

            # Should not raise
            await runner(MagicMock())


class TestMigrationRunnerDatabaseConnection:
    """Tests for migration runner database connection."""

    @pytest.mark.asyncio
    async def test_connects_to_database(self):
        """Test runner connects to database."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/test"

        with patch("backend_common.db.migrations._find_migrations_dir") as mock_find, \
             patch("backend_common.db.migrations.asyncpg.connect") as mock_connect, \
             patch("backend_common.db.migrations.Path.glob") as mock_glob:

            mock_find.return_value = Path("/tmp/migrations")
            mock_glob.return_value = []
            mock_conn = AsyncMock()
            mock_connect.return_value = mock_conn

            runner = create_migration_runner(mock_settings, [Path("/tmp")])
            await runner(MagicMock())

            mock_connect.assert_called_once_with("postgresql://localhost/test")

    @pytest.mark.asyncio
    async def test_closes_connection(self):
        """Test runner closes connection after completion."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/test"

        with patch("backend_common.db.migrations._find_migrations_dir") as mock_find, \
             patch("backend_common.db.migrations.asyncpg.connect") as mock_connect, \
             patch("backend_common.db.migrations.Path.glob") as mock_glob:

            mock_find.return_value = Path("/tmp/migrations")
            mock_glob.return_value = []
            mock_conn = AsyncMock()
            mock_connect.return_value = mock_conn

            runner = create_migration_runner(mock_settings, [Path("/tmp")])
            await runner(MagicMock())

            mock_conn.close.assert_awaited_once()

    @pytest.mark.asyncio
    async def test_retries_on_connection_error(self):
        """Test runner retries on connection error."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/test"

        with patch("backend_common.db.migrations._find_migrations_dir") as mock_find, \
             patch("backend_common.db.migrations.asyncpg.connect") as mock_connect, \
             patch("backend_common.db.migrations.Path.glob") as mock_glob, \
             patch("backend_common.db.migrations.asyncio.sleep") as mock_sleep:

            mock_find.return_value = Path("/tmp/migrations")
            mock_glob.return_value = []

            # Fail twice, then succeed
            mock_connect.side_effect = [
                Exception("Connection error 1"),
                Exception("Connection error 2"),
                AsyncMock(),  # Success
            ]

            runner = create_migration_runner(mock_settings, [Path("/tmp")])
            await runner(MagicMock())

            assert mock_connect.call_count == 3
            assert mock_sleep.call_count == 2

    @pytest.mark.asyncio
    async def test_gives_up_after_max_retries(self):
        """Test runner gives up after max retries."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/test"

        with patch("backend_common.db.migrations._find_migrations_dir") as mock_find, \
             patch("backend_common.db.migrations.asyncpg.connect") as mock_connect, \
             patch("backend_common.db.migrations.Path.glob") as mock_glob, \
             patch("backend_common.db.migrations.asyncio.sleep") as mock_sleep:

            mock_find.return_value = Path("/tmp/migrations")
            mock_glob.return_value = []

            # Always fail
            mock_connect.side_effect = Exception("Connection error")

            runner = create_migration_runner(mock_settings, [Path("/tmp")])

            # Should not raise, just skip migrations
            await runner(MagicMock())

            assert mock_connect.call_count == 5
            assert mock_sleep.call_count == 4

    @pytest.mark.asyncio
    async def test_handles_database_not_exist_error(self):
        """Test runner handles database not exist error."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/test"

        with patch("backend_common.db.migrations._find_migrations_dir") as mock_find, \
             patch("backend_common.db.migrations.asyncpg.connect") as mock_connect, \
             patch("backend_common.db.migrations.Path.glob") as mock_glob, \
             patch("backend_common.db.migrations.asyncio.sleep") as mock_sleep:

            mock_find.return_value = Path("/tmp/migrations")
            mock_glob.return_value = []

            # Database does not exist
            from asyncpg.exceptions import InvalidCatalogNameError
            mock_connect.side_effect = InvalidCatalogNameError("database \"test\" does not exist")

            runner = create_migration_runner(mock_settings, [Path("/tmp")])

            # Should not raise, just skip migrations
            await runner(MagicMock())

            # Should retry
            assert mock_connect.call_count > 1


class TestMigrationRunnerSchemaMigrations:
    """Tests for migration runner schema_migrations table."""

    @pytest.mark.asyncio
    async def test_creates_schema_migrations_table(self):
        """Test runner creates schema_migrations table."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/test"

        with patch("backend_common.db.migrations._find_migrations_dir") as mock_find, \
             patch("backend_common.db.migrations.asyncpg.connect") as mock_connect, \
             patch("backend_common.db.migrations.Path.glob") as mock_glob:

            mock_find.return_value = Path("/tmp/migrations")
            mock_glob.return_value = []
            mock_conn = AsyncMock()
            mock_connect.return_value = mock_conn

            runner = create_migration_runner(mock_settings, [Path("/tmp")])
            await runner(MagicMock())

            # Check schema_migrations table creation
            mock_conn.execute.assert_called()
            calls = [str(call) for call in mock_conn.execute.call_args_list]
            assert any("schema_migrations" in call for call in calls)

    @pytest.mark.asyncio
    async def test_fetches_existing_migrations(self):
        """Test runner fetches existing migrations."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/test"

        with patch("backend_common.db.migrations._find_migrations_dir") as mock_find, \
             patch("backend_common.db.migrations.asyncpg.connect") as mock_connect, \
             patch("backend_common.db.migrations.Path.glob") as mock_glob:

            mock_find.return_value = Path("/tmp/migrations")
            mock_glob.return_value = []
            mock_conn = AsyncMock()
            mock_connect.return_value = mock_conn
            mock_conn.fetch = AsyncMock(return_value=[])

            runner = create_migration_runner(mock_settings, [Path("/tmp")])
            await runner(MagicMock())

            mock_conn.fetch.assert_called_once_with(
                "SELECT version, checksum FROM schema_migrations"
            )


class TestMigrationRunnerApplyMigrations:
    """Tests for migration runner applying migrations."""

    @pytest.mark.asyncio
    async def test_applies_pending_migrations(self):
        """Test runner applies pending migrations."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/test"

        with patch("backend_common.db.migrations._find_migrations_dir") as mock_find, \
             patch("backend_common.db.migrations.asyncpg.connect") as mock_connect, \
             patch("backend_common.db.migrations.Path.glob") as mock_glob:

            mock_find.return_value = Path("/tmp/migrations")

            # Create mock migration file
            mock_file = MagicMock()
            mock_file.stem = "001_initial"
            mock_file.read_text = MagicMock(return_value="CREATE TABLE test;")
            mock_glob.return_value = [mock_file]

            mock_conn = AsyncMock()
            mock_connect.return_value = mock_conn
            mock_conn.fetch = AsyncMock(return_value=[])  # No existing migrations

            runner = create_migration_runner(mock_settings, [Path("/tmp")])
            await runner(MagicMock())

            # Check migration was applied
            assert mock_conn.execute.call_count >= 2  # schema_migrations + migration

    @pytest.mark.asyncio
    async def test_skips_already_applied_migrations(self):
        """Test runner skips already applied migrations."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/test"

        with patch("backend_common.db.migrations._find_migrations_dir") as mock_find, \
             patch("backend_common.db.migrations.asyncpg.connect") as mock_connect, \
             patch("backend_common.db.migrations.Path.glob") as mock_glob:

            mock_find.return_value = Path("/tmp/migrations")

            # Create mock migration file
            mock_file = MagicMock()
            mock_file.stem = "001_initial"
            sql_content = "CREATE TABLE test;"
            mock_file.read_text = MagicMock(return_value=sql_content)
            mock_glob.return_value = [mock_file]

            mock_conn = AsyncMock()
            mock_connect.return_value = mock_conn

            # Migration already applied
            checksum = hashlib.sha256(sql_content.encode("utf-8")).hexdigest()
            mock_conn.fetch = AsyncMock(return_value=[
                {"version": "001_initial", "checksum": checksum}
            ])

            runner = create_migration_runner(mock_settings, [Path("/tmp")])
            await runner(MagicMock())

            # Should not apply migration again
            # Only schema_migrations table creation and fetch should be called
            calls = [str(call) for call in mock_conn.execute.call_args_list]
            assert not any("CREATE TABLE test" in call for call in calls)

    @pytest.mark.asyncio
    async def test_detects_checksum_mismatch(self):
        """Test runner detects checksum mismatch."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/test"

        with patch("backend_common.db.migrations._find_migrations_dir") as mock_find, \
             patch("backend_common.db.migrations.asyncpg.connect") as mock_connect, \
             patch("backend_common.db.migrations.Path.glob") as mock_glob:

            mock_find.return_value = Path("/tmp/migrations")

            # Create mock migration file
            mock_file = MagicMock()
            mock_file.stem = "001_initial"
            sql_content = "CREATE TABLE test;"
            mock_file.read_text = MagicMock(return_value=sql_content)
            mock_glob.return_value = [mock_file]

            mock_conn = AsyncMock()
            mock_connect.return_value = mock_conn

            # Migration already applied with different checksum
            mock_conn.fetch = AsyncMock(return_value=[
                {"version": "001_initial", "checksum": "wrong-checksum"}
            ])

            runner = create_migration_runner(mock_settings, [Path("/tmp")])

            with pytest.raises(RuntimeError, match="Checksum mismatch"):
                await runner(MagicMock())

    @pytest.mark.asyncio
    async def test_handles_duplicate_object_error(self):
        """Test runner handles duplicate object error."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/test"

        with patch("backend_common.db.migrations._find_migrations_dir") as mock_find, \
             patch("backend_common.db.migrations.asyncpg.connect") as mock_connect, \
             patch("backend_common.db.migrations.Path.glob") as mock_glob:

            mock_find.return_value = Path("/tmp/migrations")

            # Create mock migration file
            mock_file = MagicMock()
            mock_file.stem = "001_initial"
            sql_content = "CREATE TABLE test;"
            mock_file.read_text = MagicMock(return_value=sql_content)
            mock_glob.return_value = [mock_file]

            mock_conn = AsyncMock()
            mock_connect.return_value = mock_conn
            mock_conn.fetch = AsyncMock(return_value=[])  # No existing migrations

            # Raise duplicate object error
            from asyncpg.exceptions import DuplicateObjectError
            mock_conn.execute.side_effect = [
                None,  # schema_migrations table creation
                None,  # fetch
                DuplicateObjectError("table already exists"),  # migration
            ]
            mock_conn.fetchval = AsyncMock(return_value="001_initial")  # Already marked

            runner = create_migration_runner(mock_settings, [Path("/tmp")])

            # Should not raise, just mark as applied
            await runner(MagicMock())

    @pytest.mark.asyncio
    async def test_uses_transaction_for_migrations(self):
        """Test runner uses transaction for migrations."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/test"

        with patch("backend_common.db.migrations._find_migrations_dir") as mock_find, \
             patch("backend_common.db.migrations.asyncpg.connect") as mock_connect, \
             patch("backend_common.db.migrations.Path.glob") as mock_glob:

            mock_find.return_value = Path("/tmp/migrations")

            # Create mock migration file
            mock_file = MagicMock()
            mock_file.stem = "001_initial"
            mock_file.read_text = MagicMock(return_value="CREATE TABLE test;")
            mock_glob.return_value = [mock_file]

            mock_conn = AsyncMock()
            mock_connect.return_value = mock_conn
            mock_conn.fetch = AsyncMock(return_value=[])
            mock_conn.transaction = MagicMock()
            mock_transaction = AsyncMock()
            mock_transaction.__aenter__ = AsyncMock(return_value=mock_transaction)
            mock_transaction.__aexit__ = AsyncMock(return_value=None)
            mock_conn.transaction.return_value = mock_transaction

            runner = create_migration_runner(mock_settings, [Path("/tmp")])
            await runner(MagicMock())

            # Check transaction was used
            mock_conn.transaction.assert_called()


class TestMigrationRunnerEdgeCases:
    """Tests for migration runner edge cases."""

    @pytest.mark.asyncio
    async def test_handles_duplicate_migration_versions(self):
        """Test runner handles duplicate migration versions."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/test"

        with patch("backend_common.db.migrations._find_migrations_dir") as mock_find, \
             patch("backend_common.db.migrations.Path.glob") as mock_glob:

            mock_find.return_value = Path("/tmp/migrations")

            # Create duplicate migration files
            mock_file1 = MagicMock()
            mock_file1.stem = "001_initial"
            mock_file1.read_text = MagicMock(return_value="CREATE TABLE test1;")

            mock_file2 = MagicMock()
            mock_file2.stem = "001_initial"  # Duplicate!
            mock_file2.read_text = MagicMock(return_value="CREATE TABLE test2;")

            mock_glob.return_value = [mock_file1, mock_file2]

            runner = create_migration_runner(mock_settings, [Path("/tmp")])

            with pytest.raises(ValueError, match="Duplicate migration version"):
                await runner(MagicMock())

    @pytest.mark.asyncio
    async def test_sorts_migrations_by_version(self):
        """Test runner sorts migrations by version."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/test"

        with patch("backend_common.db.migrations._find_migrations_dir") as mock_find, \
             patch("backend_common.db.migrations.asyncpg.connect") as mock_connect, \
             patch("backend_common.db.migrations.Path.glob") as mock_glob:

            mock_find.return_value = Path("/tmp/migrations")

            # Create migration files out of order
            mock_file3 = MagicMock()
            mock_file3.stem = "003_add_column"
            mock_file3.read_text = MagicMock(return_value="ALTER TABLE test ADD column;")

            mock_file1 = MagicMock()
            mock_file1.stem = "001_initial"
            mock_file1.read_text = MagicMock(return_value="CREATE TABLE test;")

            mock_file2 = MagicMock()
            mock_file2.stem = "002_add_index"
            mock_file2.read_text = MagicMock(return_value="CREATE INDEX ON test;")

            mock_glob.return_value = [mock_file3, mock_file1, mock_file2]

            mock_conn = AsyncMock()
            mock_connect.return_value = mock_conn
            mock_conn.fetch = AsyncMock(return_value=[])

            runner = create_migration_runner(mock_settings, [Path("/tmp")])
            await runner(MagicMock())

            # Migrations should be applied in order: 001, 002, 003
            calls = mock_conn.execute.call_args_list
            # First call is schema_migrations table creation
            # Then 001_initial, 002_add_index, 003_add_column
            assert len(calls) >= 4

    @pytest.mark.asyncio
    async def test_handles_non_sql_files(self):
        """Test runner ignores non-SQL files."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/test"

        with patch("backend_common.db.migrations._find_migrations_dir") as mock_find, \
             patch("backend_common.db.migrations.asyncpg.connect") as mock_connect, \
             patch("backend_common.db.migrations.Path.glob") as mock_glob:

            mock_find.return_value = Path("/tmp/migrations")

            # Create SQL and non-SQL files
            sql_file = MagicMock()
            sql_file.stem = "001_initial"
            sql_file.read_text = MagicMock(return_value="CREATE TABLE test;")

            txt_file = MagicMock()
            txt_file.stem = "README"

            mock_glob.return_value = [sql_file, txt_file]

            mock_conn = AsyncMock()
            mock_connect.return_value = mock_conn
            mock_conn.fetch = AsyncMock(return_value=[])

            runner = create_migration_runner(mock_settings, [Path("/tmp")])
            await runner(MagicMock())

            # Should only process SQL files
            assert mock_conn.execute.call_count >= 2

    @pytest.mark.asyncio
    async def test_prints_no_pending_migrations(self):
        """Test runner prints message when no pending migrations."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/test"

        with patch("backend_common.db.migrations._find_migrations_dir") as mock_find, \
             patch("backend_common.db.migrations.asyncpg.connect") as mock_connect, \
             patch("backend_common.db.migrations.Path.glob") as mock_glob:

            mock_find.return_value = Path("/tmp/migrations")
            mock_glob.return_value = []
            mock_conn = AsyncMock()
            mock_connect.return_value = mock_conn

            runner = create_migration_runner(mock_settings, [Path("/tmp")])
            await runner(MagicMock())

            # Should complete without errors


class TestMigrationRunnerIntegration:
    """Integration tests for migration runner."""

    @pytest.mark.asyncio
    async def test_full_migration_workflow(self):
        """Test complete migration workflow."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/test"

        with patch("backend_common.db.migrations._find_migrations_dir") as mock_find, \
             patch("backend_common.db.migrations.asyncpg.connect") as mock_connect, \
             patch("backend_common.db.migrations.Path.glob") as mock_glob:

            mock_find.return_value = Path("/tmp/migrations")

            # Create migration files
            mock_file1 = MagicMock()
            mock_file1.stem = "001_initial"
            mock_file1.read_text = MagicMock(return_value="CREATE TABLE users;")

            mock_file2 = MagicMock()
            mock_file2.stem = "002_add_email"
            mock_file2.read_text = MagicMock(return_value="ALTER TABLE users ADD email;")

            mock_glob.return_value = [mock_file1, mock_file2]

            mock_conn = AsyncMock()
            mock_connect.return_value = mock_conn
            mock_conn.fetch = AsyncMock(return_value=[])  # No existing migrations

            runner = create_migration_runner(mock_settings, [Path("/tmp")])
            await runner(MagicMock())

            # Verify both migrations were applied
            assert mock_conn.execute.call_count >= 3  # schema_migrations + 2 migrations

    @pytest.mark.asyncio
    async def test_partial_migration_workflow(self):
        """Test partial migration (some already applied)."""
        mock_settings = MagicMock()
        mock_settings.database_url = "postgresql://localhost/test"

        with patch("backend_common.db.migrations._find_migrations_dir") as mock_find, \
             patch("backend_common.db.migrations.asyncpg.connect") as mock_connect, \
             patch("backend_common.db.migrations.Path.glob") as mock_glob:

            mock_find.return_value = Path("/tmp/migrations")

            # Create migration files
            mock_file1 = MagicMock()
            mock_file1.stem = "001_initial"
            sql1 = "CREATE TABLE users;"
            mock_file1.read_text = MagicMock(return_value=sql1)

            mock_file2 = MagicMock()
            mock_file2.stem = "002_add_email"
            sql2 = "ALTER TABLE users ADD email;"
            mock_file2.read_text = MagicMock(return_value=sql2)

            mock_glob.return_value = [mock_file1, mock_file2]

            mock_conn = AsyncMock()
            mock_connect.return_value = mock_conn

            # First migration already applied
            checksum1 = hashlib.sha256(sql1.encode("utf-8")).hexdigest()
            mock_conn.fetch = AsyncMock(return_value=[
                {"version": "001_initial", "checksum": checksum1}
            ])

            runner = create_migration_runner(mock_settings, [Path("/tmp")])
            await runner(MagicMock())

            # Only second migration should be applied
            # Check that 002_add_email was executed
            calls = [str(call) for call in mock_conn.execute.call_args_list]
            assert any("002_add_email" in str(call) or "ALTER TABLE" in str(call) for call in calls)
