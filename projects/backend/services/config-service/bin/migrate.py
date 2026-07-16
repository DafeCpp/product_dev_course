#!/usr/bin/env python3
"""Run Config Service SQL migrations."""
from __future__ import annotations

from pathlib import Path

from backend_common.db.migrations import run_migrate_cli


def main() -> None:
    """Run migrations from the service-local migrations directory."""
    run_migrate_cli(Path(__file__).resolve().parent.parent / "migrations")


if __name__ == "__main__":
    main()
