"""Integration tests for the shared migration runners.

Unlike service tests these exercises create disposable databases directly.  A
real PostgreSQL/TimescaleDB instance is mandatory: SQL ordering, transactions
and checksums are precisely the behaviour this module is responsible for.
"""
from __future__ import annotations

import os
from pathlib import Path
from types import SimpleNamespace
from urllib.parse import urlsplit, urlunsplit
from uuid import uuid4
from unittest.mock import AsyncMock, patch

import asyncpg
import pytest
from asyncpg.exceptions import InvalidCatalogNameError

from backend_common.db.migrations import (
    _apply_migrations_cli,
    _cli_load_migrations,
    _find_migrations_dir,
    create_migration_runner,
)


def _write_migrations(directory: Path, files: dict[str, str]) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    for name, sql in files.items():
        (directory / name).write_text(sql, encoding="utf-8")
    return directory


def _database_url(base_url: str, database: str) -> str:
    parsed = urlsplit(base_url)
    return urlunsplit((parsed.scheme, parsed.netloc, f"/{database}", parsed.query, parsed.fragment))


@pytest.fixture
async def migrations_database() -> str:
    """Create and drop a database so migration metadata cannot leak between tests."""
    base_url = os.environ.get("TEST_POSTGRESQL_DSN")
    if not base_url:
        pytest.skip("TEST_POSTGRESQL_DSN is required for migration integration tests")

    database = f"backend_common_test_{uuid4().hex}"
    admin = await asyncpg.connect(base_url)
    try:
        await admin.execute(f'CREATE DATABASE "{database}"')
    finally:
        await admin.close()

    database_url = _database_url(base_url, database)
    try:
        yield database_url
    finally:
        admin = await asyncpg.connect(base_url)
        try:
            await admin.execute(
                "SELECT pg_terminate_backend(pid) FROM pg_stat_activity "
                "WHERE datname = $1 AND pid <> pg_backend_pid()",
                database,
            )
            await admin.execute(f'DROP DATABASE IF EXISTS "{database}"')
        finally:
            await admin.close()


class TestMigrationDiscovery:
    def test_finds_first_existing_directory(self, tmp_path: Path) -> None:
        first = tmp_path / "first"
        second = tmp_path / "second"
        first.mkdir()
        second.mkdir()

        assert _find_migrations_dir([tmp_path / "missing", first, second]) == first

    def test_returns_none_when_no_path_exists(self, tmp_path: Path) -> None:
        assert _find_migrations_dir([tmp_path / "one", tmp_path / "two"]) is None

    def test_cli_loader_rejects_missing_empty_and_duplicate_directories(self, tmp_path: Path) -> None:
        with pytest.raises(FileNotFoundError):
            _cli_load_migrations(tmp_path / "missing")
        with pytest.raises(ValueError, match="No .*sql"):
            _cli_load_migrations(tmp_path)

        migrations = _write_migrations(
            tmp_path / "migrations",
            {"002_second.sql": "SELECT 2;", "001_first.sql": "SELECT 1;"},
        )
        assert list(_cli_load_migrations(migrations)) == ["001_first", "002_second"]


class TestMigrationStartupFailures:
    @pytest.mark.asyncio
    async def test_startup_skips_missing_or_empty_directories(self, tmp_path: Path) -> None:
        settings = SimpleNamespace(database_url="postgresql://unused")
        empty_dir = tmp_path / "empty"
        empty_dir.mkdir()
        for migrations_dir in (tmp_path / "missing", empty_dir):
            runner = create_migration_runner(settings, [migrations_dir])
            with patch("backend_common.db.migrations.asyncpg.connect") as connect:
                await runner(None)  # type: ignore[arg-type]
            connect.assert_not_called()

    @pytest.mark.asyncio
    async def test_startup_retries_database_connection_failures(self, tmp_path: Path) -> None:
        migrations = _write_migrations(tmp_path / "migrations", {"001_initial.sql": "SELECT 1;"})
        runner = create_migration_runner(SimpleNamespace(database_url="postgresql://unavailable"), [migrations])

        with (
            patch(
                "backend_common.db.migrations.asyncpg.connect",
                side_effect=OSError("database unavailable"),
            ) as connect,
            patch("backend_common.db.migrations.asyncio.sleep", new_callable=AsyncMock) as sleep,
        ):
            await runner(None)  # type: ignore[arg-type]

        assert connect.await_count == 5
        assert sleep.await_count == 4

    @pytest.mark.asyncio
    async def test_startup_reports_missing_database_with_creation_hint(self, tmp_path: Path) -> None:
        migrations = _write_migrations(tmp_path / "migrations", {"001_initial.sql": "SELECT 1;"})
        runner = create_migration_runner(
            SimpleNamespace(database_url="postgresql://missing"),
            [migrations],
            create_db_hint="createdb service_db",
        )

        with (
            patch(
                "backend_common.db.migrations.asyncpg.connect",
                side_effect=InvalidCatalogNameError('database "service_db" does not exist'),
            ) as connect,
            patch("backend_common.db.migrations.asyncio.sleep", new_callable=AsyncMock) as sleep,
        ):
            await runner(None)  # type: ignore[arg-type]

        assert connect.await_count == 5
        assert sleep.await_count == 4


class TestMigrationRunner:
    @pytest.mark.asyncio
    @pytest.mark.integration
    async def test_applies_sorted_files_records_checksums_and_is_idempotent(
        self, migrations_database: str, tmp_path: Path
    ) -> None:
        migrations = _write_migrations(
            tmp_path / "migrations",
            {
                "002_second.sql": "CREATE TABLE second_table (id integer PRIMARY KEY);",
                "001_first.sql": "CREATE TABLE first_table (id integer PRIMARY KEY);",
            },
        )
        runner = create_migration_runner(SimpleNamespace(database_url=migrations_database), [migrations])

        await runner(None)  # type: ignore[arg-type]
        await runner(None)  # a second startup must not reapply files

        conn = await asyncpg.connect(migrations_database)
        try:
            assert await conn.fetchval("SELECT to_regclass('first_table')") == "first_table"
            assert await conn.fetchval("SELECT to_regclass('second_table')") == "second_table"
            assert await conn.fetchval("SELECT count(*) FROM schema_migrations") == 2
            assert await conn.fetchval("SELECT version FROM schema_migrations ORDER BY version LIMIT 1") == "001_first"
        finally:
            await conn.close()

    @pytest.mark.asyncio
    @pytest.mark.integration
    async def test_rejects_changed_checksum_and_rolls_back_failed_sql(
        self, migrations_database: str, tmp_path: Path
    ) -> None:
        migrations = _write_migrations(
            tmp_path / "migrations",
            {"001_broken.sql": "CREATE TABLE rolled_back_table (id integer); SELECT missing_function();"},
        )
        runner = create_migration_runner(SimpleNamespace(database_url=migrations_database), [migrations])

        with pytest.raises(asyncpg.UndefinedFunctionError):
            await runner(None)  # type: ignore[arg-type]

        conn = await asyncpg.connect(migrations_database)
        try:
            assert await conn.fetchval("SELECT to_regclass('rolled_back_table')") is None
            assert await conn.fetchval("SELECT count(*) FROM schema_migrations") == 0
        finally:
            await conn.close()

        (migrations / "001_broken.sql").write_text("SELECT 1;", encoding="utf-8")
        await runner(None)  # establish the original checksum
        (migrations / "001_broken.sql").write_text("SELECT 2;", encoding="utf-8")
        with pytest.raises(RuntimeError, match="Checksum mismatch"):
            await runner(None)  # type: ignore[arg-type]

    @pytest.mark.asyncio
    @pytest.mark.integration
    async def test_cli_dry_run_does_not_apply_and_real_run_does(
        self, migrations_database: str, tmp_path: Path, capsys: pytest.CaptureFixture[str]
    ) -> None:
        migrations = _write_migrations(
            tmp_path / "migrations", {"001_cli.sql": "CREATE TABLE cli_table (id integer);"}
        )

        await _apply_migrations_cli(migrations_database, migrations, dry_run=True)
        assert "[dry-run] Pending migration: 001_cli.sql" in capsys.readouterr().out
        conn = await asyncpg.connect(migrations_database)
        try:
            assert await conn.fetchval("SELECT to_regclass('cli_table')") is None
        finally:
            await conn.close()

        await _apply_migrations_cli(migrations_database, migrations, dry_run=False)
        conn = await asyncpg.connect(migrations_database)
        try:
            assert await conn.fetchval("SELECT to_regclass('cli_table')") == "cli_table"
        finally:
            await conn.close()
