"""Pytest configuration and fixtures for RBAC tests."""
import asyncio
from pathlib import Path
from uuid import UUID

import pytest
from testsuite.databases.pgsql import discover
import asyncpg

# Disable password complexity validation for tests
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
def database_url(pgsql):
    """Get database URL from pgsql fixture."""
    return pgsql["auth_service"].conninfo.get_uri()


@pytest.fixture
async def pool(database_url):
    """Create asyncpg pool for tests."""
    pool = await asyncpg.create_pool(database_url, min_size=2, max_size=5)
    yield pool
    await pool.close()


@pytest.fixture
async def pool_with_seed(database_url):
    """Create asyncpg pool and seed permissions and roles."""
    pool = await asyncpg.create_pool(database_url, min_size=2, max_size=5)
    
    # Seed permissions and roles if not present
    async with pool.acquire() as conn:
        count = await conn.fetchval("SELECT count(*) FROM permissions")
        if count == 0:
            await conn.execute("""
                INSERT INTO permissions (id, scope_type, category, description) VALUES
                ('users.list', 'system', 'users', 'List users'),
                ('users.create', 'system', 'users', 'Create users'),
                ('users.update', 'system', 'users', 'Update users'),
                ('users.delete', 'system', 'users', 'Delete users'),
                ('users.reset_password', 'system', 'users', 'Reset password'),
                ('users.deactivate', 'system', 'users', 'Deactivate users'),
                ('roles.manage', 'system', 'roles', 'Manage roles'),
                ('roles.assign', 'system', 'roles', 'Assign roles'),
                ('audit.read', 'system', 'audit', 'Read audit log'),
                ('projects.create', 'system', 'projects', 'Create projects'),
                ('scripts.manage', 'system', 'scripts', 'Manage scripts'),
                ('scripts.execute', 'system', 'scripts', 'Execute scripts'),
                ('scripts.view_logs', 'system', 'scripts', 'View script logs'),
                ('configs.read', 'system', 'configs', 'Read configs'),
                ('configs.write', 'system', 'configs', 'Write configs'),
                ('configs.publish', 'system', 'configs', 'Publish configs'),
                ('experiments.view', 'project', 'experiments', 'View experiments'),
                ('experiments.create', 'project', 'experiments', 'Create experiments'),
                ('experiments.update', 'project', 'experiments', 'Update experiments'),
                ('experiments.delete', 'project', 'experiments', 'Delete experiments'),
                ('experiments.archive', 'project', 'experiments', 'Archive experiments'),
                ('runs.create', 'project', 'runs', 'Create runs'),
                ('runs.update', 'project', 'runs', 'Update runs'),
                ('project.settings.update', 'project', 'settings', 'Update project settings'),
                ('project.settings.delete', 'project', 'settings', 'Delete project'),
                ('project.members.view', 'project', 'members', 'View members'),
                ('project.members.invite', 'project', 'members', 'Invite members'),
                ('project.members.remove', 'project', 'members', 'Remove members'),
                ('project.members.change_role', 'project', 'members', 'Change member role'),
                ('project.roles.manage', 'project', 'roles', 'Manage project roles')
            """)
        
        # Seed built-in system roles
        role_count = await conn.fetchval("SELECT count(*) FROM roles WHERE is_builtin = true")
        if role_count == 0:
            await conn.execute("""
                INSERT INTO roles (id, name, scope_type, project_id, is_builtin, description) VALUES
                ('00000000-0000-0000-0000-000000000001', 'superadmin', 'system', NULL, true, 'Superadmin'),
                ('00000000-0000-0000-0000-000000000002', 'admin', 'system', NULL, true, 'Admin'),
                ('00000000-0000-0000-0000-000000000003', 'operator', 'system', NULL, true, 'Operator'),
                ('00000000-0000-0000-0000-000000000004', 'auditor', 'system', NULL, true, 'Auditor'),
                ('00000000-0000-0000-0000-000000000010', 'owner', 'project', NULL, true, 'Owner'),
                ('00000000-0000-0000-0000-000000000011', 'editor', 'project', NULL, true, 'Editor'),
                ('00000000-0000-0000-0000-000000000012', 'viewer', 'project', NULL, true, 'Viewer')
            """)
            
            # Seed role_permissions for built-in roles
            await conn.execute("""
                INSERT INTO role_permissions (role_id, permission_id) VALUES
                ('00000000-0000-0000-0000-000000000002', 'users.list'),
                ('00000000-0000-0000-0000-000000000002', 'users.create'),
                ('00000000-0000-0000-0000-000000000002', 'users.update'),
                ('00000000-0000-0000-0000-000000000002', 'users.delete'),
                ('00000000-0000-0000-0000-000000000002', 'users.reset_password'),
                ('00000000-0000-0000-0000-000000000002', 'roles.manage'),
                ('00000000-0000-0000-0000-000000000002', 'roles.assign'),
                ('00000000-0000-0000-0000-000000000002', 'audit.read'),
                ('00000000-0000-0000-0000-000000000010', 'project.members.view'),
                ('00000000-0000-0000-0000-000000000010', 'project.members.invite'),
                ('00000000-0000-0000-0000-000000000010', 'project.members.remove'),
                ('00000000-0000-0000-0000-000000000010', 'project.members.change_role'),
                ('00000000-0000-0000-0000-000000000010', 'experiments.view'),
                ('00000000-0000-0000-0000-000000000010', 'experiments.create'),
                ('00000000-0000-0000-0000-000000000012', 'project.members.view'),
                ('00000000-0000-0000-0000-000000000012', 'experiments.view')
            """)
    
    yield pool
    await pool.close()


@pytest.fixture
async def grantor_user(database_url):
    """Create a user with admin role (has roles.assign permission)."""
    conn = await asyncpg.connect(database_url)
    try:
        result = await conn.fetchrow("""
            INSERT INTO users (username, email, hashed_password, password_change_required, is_active)
            VALUES ('grantoruser', 'grantor@example.com',
                    '$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG',
                    false, true)
            ON CONFLICT (username) DO UPDATE SET is_active = true
            RETURNING id
        """)
        user_id = result["id"]

        # Grant admin role (has roles.assign permission)
        await conn.execute("""
            INSERT INTO user_system_roles (user_id, role_id, granted_by, granted_at)
            VALUES ($1, '00000000-0000-0000-0000-000000000002', $1, now())
            ON CONFLICT (user_id, role_id) DO NOTHING
        """, user_id)

        return user_id
    finally:
        await conn.close()


@pytest.fixture
async def admin_user(database_url):
    """Create a user with admin role."""
    conn = await asyncpg.connect(database_url)
    try:
        result = await conn.fetchrow("""
            INSERT INTO users (username, email, hashed_password, password_change_required, is_active)
            VALUES ('adminuser2', 'adminuser2@example.com',
                    '$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG',
                    false, true)
            ON CONFLICT (username) DO UPDATE SET is_active = true
            RETURNING id
        """)
        user_id = result["id"]

        # Grant admin role
        await conn.execute("""
            INSERT INTO user_system_roles (user_id, role_id, granted_by, granted_at)
            VALUES ($1, '00000000-0000-0000-0000-000000000002', $1, now())
            ON CONFLICT (user_id, role_id) DO NOTHING
        """, user_id)

        return user_id
    finally:
        await conn.close()


@pytest.fixture
async def service_client(aiohttp_client, database_url):
    """Testsuite-style client for calling the service API."""
    settings.database_url = database_url
    app = create_app()
    return await aiohttp_client(app)


# =============================================================================
# RBAC Test Fixtures
# =============================================================================

@pytest.fixture
async def superadmin_token(service_client, database_url):
    """Create a superadmin user and return access token."""
    import asyncpg
    conn = await asyncpg.connect(str(database_url))
    try:
        # Create superadmin user
        await conn.execute("""
            INSERT INTO users (id, username, email, hashed_password, password_change_required, is_active)
            VALUES ('550e8400-e29b-41d4-a716-446655440001', 'superadmin', 'admin@example.com',
                    '$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG', false, true)
            ON CONFLICT (username) DO UPDATE SET is_active = true
        """, timeout=5)

        # Grant superadmin role
        await conn.execute("""
            INSERT INTO user_system_roles (user_id, role_id, granted_by, granted_at)
            VALUES ('550e8400-e29b-41d4-a716-446655440001',
                    '00000000-0000-0000-0000-000000000001',
                    '550e8400-e29b-41d4-a716-446655440001',
                    now())
            ON CONFLICT (user_id, role_id) DO UPDATE SET granted_at = now()
        """, timeout=5)
    finally:
        await conn.close()

    login_response = await service_client.post(
        "/auth/login",
        json={"username": "superadmin", "password": "admin123"},
    )
    assert login_response.status == 200
    return (await login_response.json())["access_token"]


@pytest.fixture
async def regular_user_token(service_client, database_url):
    """Create a regular user (no special roles) and return access token."""
    import asyncpg
    conn = await asyncpg.connect(str(database_url))
    try:
        await conn.execute("""
            INSERT INTO users (id, username, email, hashed_password, password_change_required, is_active)
            VALUES ('550e8400-e29b-41d4-a716-446655440002', 'regularuser', 'user@example.com',
                    '$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG', false, true)
            ON CONFLICT (username) DO UPDATE SET is_active = true
        """, timeout=5)
    finally:
        await conn.close()

    login_response = await service_client.post(
        "/auth/login",
        json={"username": "regularuser", "password": "admin123"},
    )
    assert login_response.status == 200
    return (await login_response.json())["access_token"]


@pytest.fixture
async def admin_user_token(service_client, database_url):
    """Create a user with admin system role and return access token."""
    import asyncpg
    conn = await asyncpg.connect(str(database_url))
    try:
        # Create admin user
        await conn.execute("""
            INSERT INTO users (id, username, email, hashed_password, password_change_required, is_active)
            VALUES ('550e8400-e29b-41d4-a716-446655440003', 'adminuser', 'adminuser@example.com',
                    '$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG', false, true)
            ON CONFLICT (username) DO UPDATE SET is_active = true
        """, timeout=5)

        # Grant admin role
        await conn.execute("""
            INSERT INTO user_system_roles (user_id, role_id, granted_by, granted_at)
            VALUES ('550e8400-e29b-41d4-a716-446655440003',
                    '00000000-0000-0000-0000-000000000002',
                    '550e8400-e29b-41d4-a716-446655440001',
                    now())
            ON CONFLICT (user_id, role_id) DO UPDATE SET granted_at = now()
        """, timeout=5)
    finally:
        await conn.close()

    login_response = await service_client.post(
        "/auth/login",
        json={"username": "adminuser", "password": "admin123"},
    )
    assert login_response.status == 200
    return (await login_response.json())["access_token"]


@pytest.fixture
async def project_data(service_client, superadmin_token, database_url):
    """Create a project and return project_id."""
    import asyncpg
    conn = await asyncpg.connect(str(database_url))
    try:
        # Create project
        result = await conn.fetchrow("""
            INSERT INTO projects (id, name, description, owner_id)
            VALUES ('660e8400-e29b-41d4-a716-446655440010', 'Test Project', 'Test Description',
                    '550e8400-e29b-41d4-a716-446655440001')
            RETURNING id
        """, timeout=5)
        project_id = result["id"]

        # Owner role is assigned automatically by the assign_project_owner_role trigger
        return str(project_id)
    finally:
        await conn.close()


@pytest.fixture
async def project_member_user_token(service_client, database_url, project_data):
    """Create a user with project member role (editor) and return access token."""
    import asyncpg
    conn = await asyncpg.connect(str(database_url))
    try:
        # Create user
        await conn.execute("""
            INSERT INTO users (id, username, email, hashed_password, password_change_required, is_active)
            VALUES ('550e8400-e29b-41d4-a716-446655440004', 'memberuser', 'member@example.com',
                    '$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG', false, true)
            ON CONFLICT (username) DO UPDATE SET is_active = true
        """, timeout=5)

        # Grant editor role in project
        await conn.execute("""
            INSERT INTO user_project_roles (user_id, project_id, role_id, granted_by, granted_at)
            VALUES ('550e8400-e29b-41d4-a716-446655440004',
                    '660e8400-e29b-41d4-a716-446655440010',
                    '00000000-0000-0000-0000-000000000011',
                    '550e8400-e29b-41d4-a716-446655440001',
                    now())
            ON CONFLICT (user_id, project_id, role_id) DO UPDATE SET granted_at = now()
        """, timeout=5)
    finally:
        await conn.close()

    login_response = await service_client.post(
        "/auth/login",
        json={"username": "memberuser", "password": "admin123"},
    )
    assert login_response.status == 200
    return (await login_response.json())["access_token"]
