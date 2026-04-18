"""Pytest configuration and fixtures for script-service integration tests."""
from __future__ import annotations

import asyncio
from pathlib import Path

import pytest
from testsuite.databases.pgsql import discover

from script_service.main import create_app
from script_service.settings import settings

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
def database_url(pgsql):
    """Get database URL from the pgsql fixture."""
    return pgsql["script_service"].conninfo.get_uri()


@pytest.fixture
async def service_client(aiohttp_client, database_url):
    """Testsuite-style aiohttp client pointing at a real test DB."""
    settings.database_url = database_url  # type: ignore[assignment]
    app = create_app()
    return await aiohttp_client(app)


# ---------------------------------------------------------------------------
# Header helpers
# ---------------------------------------------------------------------------

def make_manager_headers(user_id: str = "550e8400-e29b-41d4-a716-446655440001") -> dict[str, str]:
    """Headers for a user with scripts.manage, scripts.execute, scripts.view_logs."""
    return {
        "X-User-Id": user_id,
        "X-User-System-Permissions": "scripts.manage,scripts.execute,scripts.view_logs",
        "X-User-Permissions": "",
        "X-User-Is-Superadmin": "false",
    }


def make_executor_headers(user_id: str = "550e8400-e29b-41d4-a716-446655440002") -> dict[str, str]:
    """Headers for a user with only scripts.execute permission."""
    return {
        "X-User-Id": user_id,
        "X-User-System-Permissions": "scripts.execute",
        "X-User-Permissions": "",
        "X-User-Is-Superadmin": "false",
    }


def make_no_perm_headers(user_id: str = "550e8400-e29b-41d4-a716-446655440003") -> dict[str, str]:
    """Headers for a user with no relevant permissions."""
    return {
        "X-User-Id": user_id,
        "X-User-System-Permissions": "",
        "X-User-Permissions": "",
        "X-User-Is-Superadmin": "false",
    }


def make_superadmin_headers(user_id: str = "550e8400-e29b-41d4-a716-446655440000") -> dict[str, str]:
    """Headers for a superadmin (bypasses all permission checks)."""
    return {
        "X-User-Id": user_id,
        "X-User-System-Permissions": "",
        "X-User-Permissions": "",
        "X-User-Is-Superadmin": "true",
    }


# ---------------------------------------------------------------------------
# Script payload fixture
# ---------------------------------------------------------------------------

SAMPLE_SCRIPT_PAYLOAD: dict = {
    "name": "cleanup-old-runs",
    "description": "Removes stale experiment runs",
    "target_service": "experiment-service",
    "script_type": "python",
    "script_body": "print('hello')",
    "parameters_schema": {},
    "timeout_sec": 60,
}
