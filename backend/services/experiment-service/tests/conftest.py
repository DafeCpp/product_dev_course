import asyncio
from pathlib import Path

import pytest
from testsuite.databases.pgsql import discover

from experiment_service.main import create_app
from experiment_service.settings import settings

pytest_plugins = (
    "testsuite.pytest_plugin",
    "testsuite.databases.pgsql.pytest_plugin",
)

PG_SCHEMAS_PATH = Path(__file__).parent / "schemas" / "postgresql"


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


@pytest.fixture
async def service_client(aiohttp_client, pgsql):
    """Testsuite-style client for calling the service API."""
    conninfo = pgsql["experiment_service"].conninfo
    settings.database_url = conninfo.get_uri()
    app = create_app()
    return await aiohttp_client(app)


