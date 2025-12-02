#!/usr/bin/env python3
"""Minimal SQL migration runner for Experiment Service."""
# pyright: reportMissingImports=false
from __future__ import annotations

import argparse
import asyncio
import hashlib
import os
from pathlib import Path
from typing import Dict

import asyncpg


def _default_migrations_dir() -> Path:
    return Path(__file__).resolve().parent.parent / "migrations"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Apply SQL migrations sequentially.")
    parser.add_argument(
        "--database-url",
        "-d",
        default=os.getenv("DATABASE_URL"),
        help="PostgreSQL connection string. Defaults to DATABASE_URL env variable.",
    )
    parser.add_argument(
        "--migrations-dir",
        "-m",
        type=Path,
        default=_default_migrations_dir(),
        help="Directory with *.sql migrations (sorted lexicographically).",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Only list pending migrations without applying.",
    )
    return parser.parse_args()


async def ensure_schema_table(conn: asyncpg.Connection) -> None:
    await conn.execute(
        """
        CREATE TABLE IF NOT EXISTS schema_migrations (
            version text PRIMARY KEY,
            checksum text NOT NULL,
            applied_at timestamptz NOT NULL DEFAULT now()
        );
        """
    )


def load_migrations(directory: Path) -> Dict[str, Path]:
    if not directory.exists():
        raise FileNotFoundError(f"Migrations directory does not exist: {directory}")
    migrations: Dict[str, Path] = {}
    for path in sorted(directory.glob("*.sql")):
        version = path.stem
        if version in migrations:
            raise ValueError(f"Duplicate migration version detected: {version}")
        migrations[version] = path
    if not migrations:
        raise ValueError(f"No *.sql files found in {directory}")
    return migrations


async def apply_migrations(database_url: str, migrations_dir: Path, dry_run: bool) -> None:
    migrations = load_migrations(migrations_dir)
    conn = await asyncpg.connect(database_url)
    try:
        await ensure_schema_table(conn)
        rows = await conn.fetch("SELECT version, checksum FROM schema_migrations")
        applied = {row["version"]: row["checksum"] for row in rows}
        pending = []
        for version, path in migrations.items():
            sql = path.read_text(encoding="utf-8")
            checksum = hashlib.sha256(sql.encode("utf-8")).hexdigest()
            if version in applied:
                if applied[version] != checksum:
                    raise RuntimeError(
                        f"Checksum mismatch for {version}: "
                        f"{applied[version]} (db) != {checksum} (file)"
                    )
                continue
            pending.append((version, path, sql, checksum))

        if not pending:
            print("No pending migrations.")
            return

        for version, path, sql, checksum in pending:
            if dry_run:
                print(f"[dry-run] Pending migration: {path.name}")
                continue
            print(f"Applying {path.name}...")
            async with conn.transaction():
                await conn.execute(sql)
                await conn.execute(
                    "INSERT INTO schema_migrations (version, checksum) VALUES ($1, $2)",
                    version,
                    checksum,
                )
        if dry_run:
            print(f"{len(pending)} migration(s) pending.")
        else:
            print(f"Applied {len(pending)} migration(s).")
    finally:
        await conn.close()


async def main_async() -> None:
    args = parse_args()
    if not args.database_url:
        raise SystemExit("Database URL must be provided via --database-url or DATABASE_URL env.")
    await apply_migrations(args.database_url, args.migrations_dir, args.dry_run)


def main() -> None:
    asyncio.run(main_async())


if __name__ == "__main__":
    main()

