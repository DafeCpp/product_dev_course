"""Unit tests for :mod:`backend_common.db.migrations`."""

from __future__ import annotations

import hashlib
from pathlib import Path
from unittest.mock import AsyncMock, MagicMock, patch

import pytest

from backend_common.db.migrations import (
    _find_migrations_dir,
    create_migration_runner,
)


def _settings() -> MagicMock:
    settings = MagicMock()
    settings.database_url = "postgresql://localhost/test"
    return settings


def _write_migrations(directory: Path, files: dict[str, str] | None = None) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    for name, sql in (files or {"001_initial.sql": "CREATE TABLE test;"}).items():
        (directory / name).write_text(sql, encoding="utf-8")
    return directory


def _connection(rows: list[dict[str, str]] | None = None) -> AsyncMock:
    conn = AsyncMock()
    conn.fetch = AsyncMock(return_value=rows or [])
    transaction = MagicMock()
    transaction.__aenter__ = AsyncMock(return_value=transaction)
    transaction.__aexit__ = AsyncMock(return_value=None)
    conn.transaction = MagicMock(return_value=transaction)
    return conn


class TestFindMigrationsDir:
    def test_finds_first_existing_directory(self, tmp_path: Path) -> None:
        missing = tmp_path / "missing"
        first = tmp_path / "first"
        second = tmp_path / "second"
        first.mkdir()
        second.mkdir()

        assert _find_migrations_dir([missing, first, second]) == first

    def test_returns_none_when_not_found(self, tmp_path: Path) -> None:
        assert _find_migrations_dir([tmp_path / "one", tmp_path / "two"]) is None

    def test_handles_empty_paths(self) -> None:
        assert _find_migrations_dir([]) is None


class TestCreateMigrationRunner:
    @pytest.mark.parametrize("paths", [[], [Path("/tmp")], [Path("/one"), Path("/two")]])
    def test_returns_callable(self, paths: list[Path]) -> None:
        assert callable(create_migration_runner(_settings(), paths))

    def test_accepts_create_db_hint(self) -> None:
        runner = create_migration_runner(
            _settings(),
            [Path("/tmp")],
            create_db_hint="CREATE DATABASE test",
        )

        assert callable(runner)


class TestMigrationDiscovery:
    @pytest.mark.asyncio
    async def test_missing_directory_skips_migrations(self, tmp_path: Path) -> None:
        runner = create_migration_runner(_settings(), [tmp_path / "missing"])

        with patch("backend_common.db.migrations.asyncpg.connect") as connect:
            await runner(MagicMock())

        connect.assert_not_called()

    @pytest.mark.asyncio
    async def test_empty_directory_skips_migrations(self, tmp_path: Path) -> None:
        runner = create_migration_runner(_settings(), [tmp_path])

        with patch("backend_common.db.migrations.asyncpg.connect") as connect:
            await runner(MagicMock())

        connect.assert_not_called()

    @pytest.mark.asyncio
    async def test_ignores_non_sql_files(self, tmp_path: Path) -> None:
        (tmp_path / "README.md").write_text("not a migration", encoding="utf-8")
        runner = create_migration_runner(_settings(), [tmp_path])

        with patch("backend_common.db.migrations.asyncpg.connect") as connect:
            await runner(MagicMock())

        connect.assert_not_called()

    @pytest.mark.asyncio
    async def test_rejects_duplicate_versions_from_real_paths(self, tmp_path: Path) -> None:
        first = _write_migrations(tmp_path / "first") / "001_initial.sql"
        second = _write_migrations(tmp_path / "second") / "001_initial.sql"
        runner = create_migration_runner(_settings(), [tmp_path])

        with patch("backend_common.db.migrations.Path.glob", return_value=[second, first]):
            with pytest.raises(ValueError, match="Duplicate migration version"):
                await runner(MagicMock())


class TestDatabaseConnection:
    @pytest.mark.asyncio
    async def test_connects_and_closes_connection(self, tmp_path: Path) -> None:
        migrations = _write_migrations(tmp_path)
        conn = _connection()
        runner = create_migration_runner(_settings(), [migrations])

        with patch("backend_common.db.migrations.asyncpg.connect", return_value=conn) as connect:
            await runner(MagicMock())

        connect.assert_awaited_once_with("postgresql://localhost/test")
        conn.close.assert_awaited_once()

    @pytest.mark.asyncio
    async def test_retries_on_connection_error(self, tmp_path: Path) -> None:
        migrations = _write_migrations(tmp_path)
        conn = _connection()
        runner = create_migration_runner(_settings(), [migrations])

        with (
            patch(
                "backend_common.db.migrations.asyncpg.connect",
                side_effect=[Exception("first"), Exception("second"), conn],
            ) as connect,
            patch("backend_common.db.migrations.asyncio.sleep", new_callable=AsyncMock) as sleep,
        ):
            await runner(MagicMock())

        assert connect.await_count == 3
        assert sleep.await_count == 2

    @pytest.mark.asyncio
    async def test_gives_up_after_max_retries(self, tmp_path: Path) -> None:
        migrations = _write_migrations(tmp_path)
        runner = create_migration_runner(_settings(), [migrations])

        with (
            patch(
                "backend_common.db.migrations.asyncpg.connect",
                side_effect=Exception("unavailable"),
            ) as connect,
            patch("backend_common.db.migrations.asyncio.sleep", new_callable=AsyncMock) as sleep,
        ):
            await runner(MagicMock())

        assert connect.await_count == 5
        assert sleep.await_count == 4

    @pytest.mark.asyncio
    async def test_retries_when_database_does_not_exist(self, tmp_path: Path) -> None:
        from asyncpg.exceptions import InvalidCatalogNameError

        migrations = _write_migrations(tmp_path)
        runner = create_migration_runner(_settings(), [migrations])

        with (
            patch(
                "backend_common.db.migrations.asyncpg.connect",
                side_effect=InvalidCatalogNameError('database "test" does not exist'),
            ) as connect,
            patch("backend_common.db.migrations.asyncio.sleep", new_callable=AsyncMock),
        ):
            await runner(MagicMock())

        assert connect.await_count == 5


class TestMigrationApplication:
    @pytest.mark.asyncio
    async def test_creates_and_reads_schema_migrations(self, tmp_path: Path) -> None:
        migrations = _write_migrations(tmp_path)
        conn = _connection()
        runner = create_migration_runner(_settings(), [migrations])

        with patch("backend_common.db.migrations.asyncpg.connect", return_value=conn):
            await runner(MagicMock())

        assert "schema_migrations" in conn.execute.await_args_list[0].args[0]
        conn.fetch.assert_awaited_once_with("SELECT version, checksum FROM schema_migrations")

    @pytest.mark.asyncio
    async def test_applies_migrations_in_filename_order(self, tmp_path: Path) -> None:
        sql_by_file = {
            "003_last.sql": "SELECT 3;",
            "001_first.sql": "SELECT 1;",
            "002_second.sql": "SELECT 2;",
        }
        migrations = _write_migrations(tmp_path, sql_by_file)
        conn = _connection()
        runner = create_migration_runner(_settings(), [migrations])

        with patch("backend_common.db.migrations.asyncpg.connect", return_value=conn):
            await runner(MagicMock())

        executed_sql = [call.args[0] for call in conn.execute.await_args_list]
        assert [sql for sql in executed_sql if sql in sql_by_file.values()] == [
            "SELECT 1;",
            "SELECT 2;",
            "SELECT 3;",
        ]
        assert conn.transaction.call_count == 3

    @pytest.mark.asyncio
    async def test_skips_already_applied_migration(self, tmp_path: Path) -> None:
        sql = "CREATE TABLE test;"
        migrations = _write_migrations(tmp_path, {"001_initial.sql": sql})
        checksum = hashlib.sha256(sql.encode("utf-8")).hexdigest()
        conn = _connection([{"version": "001_initial", "checksum": checksum}])
        runner = create_migration_runner(_settings(), [migrations])

        with patch("backend_common.db.migrations.asyncpg.connect", return_value=conn):
            await runner(MagicMock())

        assert sql not in [call.args[0] for call in conn.execute.await_args_list]
        conn.transaction.assert_not_called()

    @pytest.mark.asyncio
    async def test_detects_checksum_mismatch(self, tmp_path: Path) -> None:
        migrations = _write_migrations(tmp_path)
        conn = _connection([{"version": "001_initial", "checksum": "wrong"}])
        runner = create_migration_runner(_settings(), [migrations])

        with patch("backend_common.db.migrations.asyncpg.connect", return_value=conn):
            with pytest.raises(RuntimeError, match="Checksum mismatch"):
                await runner(MagicMock())

        conn.close.assert_awaited_once()

    @pytest.mark.asyncio
    async def test_applies_only_pending_migrations(self, tmp_path: Path) -> None:
        first_sql = "SELECT 1;"
        second_sql = "SELECT 2;"
        migrations = _write_migrations(
            tmp_path,
            {"001_first.sql": first_sql, "002_second.sql": second_sql},
        )
        checksum = hashlib.sha256(first_sql.encode("utf-8")).hexdigest()
        conn = _connection([{"version": "001_first", "checksum": checksum}])
        runner = create_migration_runner(_settings(), [migrations])

        with patch("backend_common.db.migrations.asyncpg.connect", return_value=conn):
            await runner(MagicMock())

        executed_sql = [call.args[0] for call in conn.execute.await_args_list]
        assert first_sql not in executed_sql
        assert second_sql in executed_sql
        conn.transaction.assert_called_once()

    @pytest.mark.asyncio
    async def test_marks_existing_objects_as_applied(self, tmp_path: Path) -> None:
        from asyncpg.exceptions import DuplicateTableError

        migrations = _write_migrations(tmp_path)
        conn = _connection()
        conn.execute.side_effect = [None, DuplicateTableError("table already exists"), None]
        conn.fetchval = AsyncMock(return_value=None)
        runner = create_migration_runner(_settings(), [migrations])

        with patch("backend_common.db.migrations.asyncpg.connect", return_value=conn):
            await runner(MagicMock())

        conn.fetchval.assert_awaited_once_with(
            "SELECT version FROM schema_migrations WHERE version = $1",
            "001_initial",
        )
        assert "ON CONFLICT" in conn.execute.await_args_list[-1].args[0]

    @pytest.mark.asyncio
    async def test_uses_transaction_for_each_migration(self, tmp_path: Path) -> None:
        migrations = _write_migrations(
            tmp_path,
            {"001_first.sql": "SELECT 1;", "002_second.sql": "SELECT 2;"},
        )
        conn = _connection()
        runner = create_migration_runner(_settings(), [migrations])

        with patch("backend_common.db.migrations.asyncpg.connect", return_value=conn):
            await runner(MagicMock())

        assert conn.transaction.call_count == 2
