#!/usr/bin/env python3
from pathlib import Path

from backend_common.db.migrations import run_migrate_cli

if __name__ == "__main__":
    run_migrate_cli(Path(__file__).resolve().parent.parent / "migrations")
