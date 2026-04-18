"""Top-level conftest: register testsuite plugins so pytest accepts --postgresql."""
import pytest

# Monkeypatch testsuite to use TRUNCATE CASCADE for tables with foreign keys
# This must be done before importing testsuite modules
import testsuite.databases.pgsql.control as control
control.TRUNCATE_SQL_TEMPLATE = 'TRUNCATE TABLE {tables} RESTART IDENTITY CASCADE'

pytest_plugins = (
    "testsuite.pytest_plugin",
    "testsuite.databases.pgsql.pytest_plugin",
)


@pytest.fixture(scope="session")
def pgsql_local(pgsql_local_create):
    """Create PostgreSQL database for tests with proper CASCADE truncate support."""
    from testsuite.databases.pgsql import discover
    from pathlib import Path
    
    PG_SCHEMAS_PATH = Path(__file__).parent / "tests" / "integration" / "schemas" / "postgresql"
    
    databases = discover.find_schemas(
        service_name=None,
        schema_dirs=[PG_SCHEMAS_PATH],
    )
    return pgsql_local_create(list(databases.values()))
