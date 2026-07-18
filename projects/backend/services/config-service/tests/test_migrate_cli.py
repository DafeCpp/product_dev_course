"""Tests for the config-service migration CLI wrapper."""
from __future__ import annotations

from pathlib import Path
from unittest.mock import patch

from bin.migrate import main


def test_main_uses_service_migrations_directory() -> None:
    expected = Path(__file__).resolve().parents[1] / "migrations"

    with patch("bin.migrate.run_migrate_cli") as run_migrate_cli:
        main()

    run_migrate_cli.assert_called_once_with(expected)
