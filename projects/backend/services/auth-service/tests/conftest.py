"""Pytest configuration and fixtures."""
import asyncio
from pathlib import Path

import pytest
from testsuite.databases.pgsql import discover

# Disable password complexity validation for tests so that simple
# passwords like "testpass123" are accepted without requiring uppercase.
import auth_service.domain.dto as _dto_module
_dto_module.PASSWORD_COMPLEXITY_ENABLED = False

from auth_service.main import create_app
from auth_service.settings import settings

pytest_plugins = (
    "testsuite.pytest_plugin",
    "testsuite.databases.pgsql.pytest_plugin",
)

PG_SCHEMAS_PATH = Path(__file__).parent / "schemas" / "postgresql"


@pytest.fixture(scope="session")
def event_loop():
    """Create event loop for the test session."""
    loop = asyncio.new_event_loop()
    yield loop
    loop.close()


@pytest.fixture(scope="session")
def pgsql_local(pgsql_local_create):
    """Create PostgreSQL database for tests."""
    databases = discover.find_schemas(
        service_name=None,
        schema_dirs=[PG_SCHEMAS_PATH],
    )
    return pgsql_local_create(list(databases.values()))


@pytest.fixture
async def service_client(aiohttp_client, pgsql):
    """Testsuite-style client for calling the service API."""
    conninfo = pgsql["auth_service"].conninfo
    settings.database_url = conninfo.get_uri()
    app = create_app()
    return await aiohttp_client(app)

