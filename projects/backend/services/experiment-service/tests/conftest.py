import asyncio
from pathlib import Path

import pytest
from testsuite.databases.pgsql import discover, service as pgsql_service

from experiment_service.main import create_app
from experiment_service.settings import settings

pytest_plugins = (
    "testsuite.pytest_plugin",
    "testsuite.databases.pgsql.pytest_plugin",
)

_PGSQL_CONFIG_DIR = Path(__file__).parent / "pgsql_config"


def pytest_service_register(register_service):
    """Override postgresql service to use custom config dir with TimescaleDB."""
    def create_pgsql_service_with_timescaledb(service_name, working_dir, settings=None, env=None):
        return pgsql_service.create_pgsql_service(
            service_name,
            working_dir,
            settings=settings,
            env={**(env or {}), "POSTGRESQL_CONFIGS_DIR": str(_PGSQL_CONFIG_DIR)},
        )

    register_service("postgresql", create_pgsql_service_with_timescaledb)

PG_SCHEMAS_PATH = Path(__file__).parent / "schemas" / "postgresql"

class _TimescaleInternalTables:
    """
    testsuite truncates all BASE TABLEs between tests, based on information_schema.tables.
    After `CREATE EXTENSION timescaledb`, internal schemas like `_timescaledb_catalog` appear.
    Truncating those internal tables can crash or corrupt the DB; we exclude them by prefix.
    """

    def __contains__(self, item: object) -> bool:  # used by testsuite ConnectionWrapper.initialize()
        return isinstance(item, str) and item.startswith("_timescaledb_")


@pytest.fixture(scope="session")
def event_loop():
    loop = asyncio.new_event_loop()
    yield loop
    loop.close()


@pytest.fixture(scope="session")
def pgsql_local(pgsql_local_create):
    databases = discover.find_schemas(
        service_name=None,
        schema_dirs=[PG_SCHEMAS_PATH],
    )
    return pgsql_local_create(list(databases.values()))


@pytest.fixture(scope="session")
def pgsql_cleanup_exclude_tables():
    # Exclude TimescaleDB internal schemas from testsuite TRUNCATE list.
    return _TimescaleInternalTables()


@pytest.fixture
async def service_client(aiohttp_client, pgsql):
    """Testsuite-style client for calling the service API."""
    conninfo = pgsql["experiment_service"].conninfo
    settings.database_url = conninfo.get_uri()
    app = create_app()
    return await aiohttp_client(app)


