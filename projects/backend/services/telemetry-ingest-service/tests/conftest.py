from __future__ import annotations

import asyncio
from pathlib import Path

import pytest
from testsuite.databases.pgsql import discover

from telemetry_ingest_service.main import create_app
from telemetry_ingest_service.settings import settings

pytest_plugins = (
    "testsuite.pytest_plugin",
    "testsuite.databases.pgsql.pytest_plugin",
)

PG_SCHEMAS_PATH = Path(__file__).parent / "schemas" / "postgresql"

class _TimescaleInternalTables:
    """
    Exclude TimescaleDB internal schemas (e.g. `_timescaledb_catalog`) from testsuite TRUNCATE list.
    """

    def __contains__(self, item: object) -> bool:
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
    return _TimescaleInternalTables()


@pytest.fixture
async def service_client(aiohttp_client, pgsql):
    conninfo = pgsql["telemetry_ingest_service"].conninfo
    settings.database_url = conninfo.get_uri()
    app = create_app()
    return await aiohttp_client(app)

