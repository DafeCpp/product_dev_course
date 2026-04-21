import asyncio
import json
from pathlib import Path

import asyncpg
import pytest
from testsuite.databases.pgsql import discover, service as pgsql_service

from config_service.main import create_app
from config_service.settings import settings

pytest_plugins = (
    "testsuite.pytest_plugin",
    "testsuite.databases.pgsql.pytest_plugin",
)

_PGSQL_CONFIG_DIR = Path(__file__).parent / "pgsql_config"
PG_SCHEMAS_PATH = Path(__file__).parent / "schemas" / "postgresql"

_FEATURE_FLAG_SCHEMA = {
    "$schema": "https://json-schema.org/draft/2020-12/schema",
    "type": "object",
    "required": ["enabled"],
    "properties": {
        "enabled": {"type": "boolean"},
    },
    "additionalProperties": False,
}

_QOS_SCHEMA = {
    "$schema": "https://json-schema.org/draft/2020-12/schema",
    "type": "object",
    "required": ["__default__"],
    "additionalProperties": {"$ref": "#/$defs/qosSettings"},
    "properties": {
        "__default__": {"$ref": "#/$defs/qosSettings"},
    },
    "$defs": {
        "qosSettings": {
            "type": "object",
            "required": ["timeout_ms", "retries"],
            "properties": {
                "timeout_ms": {"type": "integer", "minimum": 1, "maximum": 600000},
                "retries": {"type": "integer", "minimum": 0, "maximum": 10},
            },
            "additionalProperties": False,
        }
    },
}


def pytest_service_register(register_service):
    def create_pgsql_service(service_name, working_dir, settings=None, env=None):
        return pgsql_service.create_pgsql_service(
            service_name,
            working_dir,
            settings=settings,
            env={**(env or {}), "POSTGRESQL_CONFIGS_DIR": str(_PGSQL_CONFIG_DIR)},
        )

    register_service("postgresql", create_pgsql_service)


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
    """Test client with seed schemas pre-loaded before each test."""
    conninfo = pgsql["config_service"].conninfo
    db_url = conninfo.get_uri()
    settings.database_url = db_url  # type: ignore[assignment]

    # Re-insert seed schemas since testsuite truncates tables between tests
    conn = await asyncpg.connect(db_url)
    try:
        for config_type, schema in [
            ("feature_flag", _FEATURE_FLAG_SCHEMA),
            ("qos", _QOS_SCHEMA),
        ]:
            await conn.execute(
                """
                INSERT INTO config_schemas (config_type, schema, version, is_active, created_by)
                VALUES ($1, $2::jsonb, 1, true, 'system')
                ON CONFLICT DO NOTHING
                """,
                config_type,
                json.dumps(schema),
            )
    finally:
        await conn.close()

    app = create_app()
    return await aiohttp_client(app)
