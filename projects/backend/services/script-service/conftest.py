"""Top-level conftest: register testsuite plugins so pytest accepts --postgresql."""
import pytest

pytest_plugins = (
    "testsuite.pytest_plugin",
    "testsuite.databases.pgsql.pytest_plugin",
)
