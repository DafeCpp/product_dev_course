"""Unit tests for PermissionService."""
import pytest
from uuid import uuid4, UUID

from auth_service.core.exceptions import ConflictError, ForbiddenError, NotFoundError
from auth_service.domain.models import ScopeType, SUPERADMIN_ROLE_ID, ADMIN_ROLE_ID
from auth_service.repositories.permissions import PermissionRepository
from auth_service.repositories.roles import RoleRepository
from auth_service.repositories.user_roles import UserRoleRepository
from auth_service.services.permission import PermissionService
from auth_service.settings import settings
import asyncpg


@pytest.fixture
async def pool(database_url):
    """Create asyncpg pool for tests."""
    pool = await asyncpg.create_pool(database_url, min_size=2, max_size=5)
    yield pool
    await pool.close()


@pytest.fixture
async def permission_service(pool_with_seed):
    """Create PermissionService with test database."""
    return PermissionService(
        PermissionRepository(pool_with_seed),
        RoleRepository(pool_with_seed),
        UserRoleRepository(pool_with_seed),
    )


@pytest.fixture
async def test_user(database_url):
    """Create a test user."""
    conn = await asyncpg.connect(database_url)
    try:
        result = await conn.fetchrow("""
            INSERT INTO users (username, email, hashed_password, password_change_required, is_active)
            VALUES ('testuser', 'test@example.com',
                    '$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG',
                    false, true)
            RETURNING id
        """)
        return result["id"]
    finally:
        await conn.close()



class TestGetEffectivePermissions:
    """Tests for get_effective_permissions method."""

    @pytest.mark.asyncio
    async def test_superadmin_has_empty_lists(self, permission_service, test_user, database_url):
        """Superadmin should get empty lists (all permissions implicit)."""
        conn = await asyncpg.connect(database_url)
        try:
            # Grant superadmin role
            await conn.execute("""
                INSERT INTO user_system_roles (user_id, role_id, granted_by, granted_at)
                VALUES ($1, '00000000-0000-0000-0000-000000000001', $1, now())
            """, test_user)
        finally:
            await conn.close()

        result = await permission_service.get_effective_permissions(test_user)
        assert result.is_superadmin is True
        assert result.system_permissions == []
        assert result.project_permissions == []

    @pytest.mark.asyncio
    async def test_user_with_admin_role_gets_permissions(self, permission_service, test_user, database_url):
        """User with admin role should get admin permissions."""
        conn = await asyncpg.connect(database_url)
        try:
            # Grant admin role
            await conn.execute("""
                INSERT INTO user_system_roles (user_id, role_id, granted_by, granted_at)
                VALUES ($1, '00000000-0000-0000-0000-000000000002', $1, now())
            """, test_user)
        finally:
            await conn.close()

        result = await permission_service.get_effective_permissions(test_user)
        assert result.is_superadmin is False
        assert "users.list" in result.system_permissions
        assert "users.create" in result.system_permissions
        assert "roles.assign" in result.system_permissions

    @pytest.mark.asyncio
    async def test_user_with_project_role_gets_permissions(self, permission_service, test_user, database_url):
        """User with project role should get project permissions."""
        conn = await asyncpg.connect(database_url)
        try:
            # Create project (trigger automatically assigns owner role)
            proj_result = await conn.fetchrow("""
                INSERT INTO projects (name, description, owner_id)
                VALUES ('Test Project', 'Test', $1)
                RETURNING id
            """, test_user)
            project_id = proj_result["id"]
        finally:
            await conn.close()

        result = await permission_service.get_effective_permissions(test_user, project_id)
        assert "experiments.view" in result.project_permissions
        assert "experiments.create" in result.project_permissions
        assert "project.members.view" in result.project_permissions

    @pytest.mark.asyncio
    async def test_expired_role_not_included(self, permission_service, test_user, database_url):
        """Expired roles should not contribute permissions."""
        from datetime import datetime, timedelta, timezone
        conn = await asyncpg.connect(database_url)
        try:
            # Grant expired admin role
            expired_time = datetime.now(timezone.utc) - timedelta(hours=1)
            await conn.execute("""
                INSERT INTO user_system_roles (user_id, role_id, granted_by, granted_at, expires_at)
                VALUES ($1, '00000000-0000-0000-0000-000000000002', $1, now(), $2)
            """, test_user, expired_time)
        finally:
            await conn.close()

        result = await permission_service.get_effective_permissions(test_user)
        assert "users.list" not in result.system_permissions


class TestGrantSystemRole:
    """Tests for grant_system_role method."""

    @pytest.mark.asyncio
    async def test_grant_system_role_requires_permission(self, permission_service, test_user, database_url):
        """User without roles.assign cannot grant roles."""
        with pytest.raises(ForbiddenError, match="Missing permission"):
            await permission_service.grant_system_role(
                test_user, test_user, ADMIN_ROLE_ID,
            )

    @pytest.mark.asyncio
    async def test_grant_system_role_success(self, permission_service, test_user, grantor_user, database_url):
        """User with roles.assign can grant roles."""
        result = await permission_service.grant_system_role(
            grantor_user, test_user, ADMIN_ROLE_ID,
        )
        assert result.user_id == test_user
        assert result.role_id == ADMIN_ROLE_ID

    @pytest.mark.asyncio
    async def test_cannot_grant_project_role_as_system(self, permission_service, test_user, grantor_user, database_url):
        """Cannot grant project role as system role."""
        with pytest.raises(ForbiddenError, match="Cannot assign a project role"):
            await permission_service.grant_system_role(
                grantor_user, test_user, 
                UUID("00000000-0000-0000-0000-000000000010"),  # owner role
            )


class TestRevokeSystemRole:
    """Tests for revoke_system_role method."""

    @pytest.mark.asyncio
    async def test_revoke_system_role_requires_permission(self, permission_service, test_user, database_url):
        """User without roles.assign cannot revoke roles."""
        # test_user has no roles, so should not have roles.assign permission

        with pytest.raises(ForbiddenError, match="Missing permission"):
            await permission_service.revoke_system_role(
                test_user, test_user, ADMIN_ROLE_ID,
            )

    @pytest.mark.asyncio
    async def test_cannot_revoke_last_superadmin(self, permission_service, test_user, database_url):
        """Cannot revoke the last superadmin role."""
        conn = await asyncpg.connect(database_url)
        try:
            # Make test_user superadmin
            await conn.execute("""
                INSERT INTO user_system_roles (user_id, role_id, granted_by, granted_at)
                VALUES ($1, '00000000-0000-0000-0000-000000000001', $1, now())
            """, test_user)
            
            # Also grant admin role to test_user for permission
            await conn.execute("""
                INSERT INTO user_system_roles (user_id, role_id, granted_by, granted_at)
                VALUES ($1, '00000000-0000-0000-0000-000000000002', $1, now())
            """, test_user)
        finally:
            await conn.close()

        with pytest.raises(ConflictError, match="Cannot revoke the last superadmin"):
            await permission_service.revoke_system_role(
                test_user, test_user, SUPERADMIN_ROLE_ID,
            )


class TestGrantProjectRole:
    """Tests for grant_project_role method."""

    @pytest.mark.asyncio
    async def test_grant_project_role_requires_permission(self, permission_service, test_user, grantor_user, database_url):
        """User without project.members.change_role cannot grant project roles."""
        conn = await asyncpg.connect(database_url)
        try:
            # Create project with grantor_user as owner (so test_user is not owner)
            proj_result = await conn.fetchrow("""
                INSERT INTO projects (name, description, owner_id)
                VALUES ('Test Project', 'Test', $1)
                RETURNING id
            """, grantor_user)
            project_id = proj_result["id"]
        finally:
            await conn.close()

        # test_user is not a member of the project, so should not have project.members.change_role
        with pytest.raises(ForbiddenError, match="Missing permission"):
            await permission_service.grant_project_role(
                test_user, project_id, test_user,
                UUID("00000000-0000-0000-0000-000000000011"),  # editor role
            )


class TestCreateCustomRole:
    """Tests for create_custom_role method."""

    @pytest.mark.asyncio
    async def test_create_custom_system_role_requires_roles_manage(
        self, permission_service, test_user
    ):
        """Creating custom system role requires roles.manage permission."""
        with pytest.raises(ForbiddenError, match="Missing permission"):
            await permission_service.create_custom_role(
                creator_id=test_user,
                name="Custom Role",
                scope_type=ScopeType.SYSTEM,
                permissions=["users.list"],
            )

    @pytest.mark.asyncio
    async def test_create_custom_system_role_success(
        self, permission_service, test_user, grantor_user
    ):
        """User with roles.manage can create custom system role."""
        role = await permission_service.create_custom_role(
            creator_id=grantor_user,
            name="Custom System Role",
            scope_type=ScopeType.SYSTEM,
            permissions=["users.list", "audit.read"],
            description="Test custom role",
        )
        assert role.name == "Custom System Role"
        assert role.scope_type == ScopeType.SYSTEM
        assert not role.is_builtin

    @pytest.mark.asyncio
    async def test_create_custom_role_with_invalid_permission(
        self, permission_service, grantor_user
    ):
        """Creating role with invalid permission should fail."""
        with pytest.raises(NotFoundError, match="Unknown permissions"):
            await permission_service.create_custom_role(
                creator_id=grantor_user,
                name="Invalid Role",
                scope_type=ScopeType.SYSTEM,
                permissions=["nonexistent.permission"],
            )

    @pytest.mark.asyncio
    async def test_create_custom_project_role_requires_project_roles_manage(
        self, permission_service, test_user, database_url
    ):
        """Creating custom project role requires project.roles.manage."""
        conn = await asyncpg.connect(database_url)
        try:
            # Create project
            proj_result = await conn.fetchrow("""
                INSERT INTO projects (name, description, owner_id)
                VALUES ('Test Project', 'Test', $1)
                RETURNING id
            """, test_user)
            project_id = proj_result["id"]
        finally:
            await conn.close()

        with pytest.raises(ForbiddenError, match="Missing permission"):
            await permission_service.create_custom_role(
                creator_id=test_user,
                name="Custom Project Role",
                scope_type=ScopeType.PROJECT,
                permissions=["experiments.view"],
                project_id=project_id,
            )


class TestDeleteCustomRole:
    """Tests for delete_custom_role method."""

    @pytest.mark.asyncio
    async def test_cannot_delete_builtin_role(self, permission_service, grantor_user, database_url):
        """Cannot delete built-in roles."""
        with pytest.raises(ForbiddenError, match="Cannot delete built-in roles"):
            await permission_service.delete_custom_role(
                grantor_user, ADMIN_ROLE_ID,
            )

    @pytest.mark.asyncio
    async def test_delete_custom_role_success(
        self, permission_service, grantor_user, database_url
    ):
        """Successfully delete custom role."""
        conn = await asyncpg.connect(database_url)
        try:
            # Create custom role
            role_result = await conn.fetchrow("""
                INSERT INTO roles (name, scope_type, is_builtin, created_by, description)
                VALUES ('Temp Role', 'system', false, $1, 'Test')
                RETURNING id
            """, grantor_user)
            role_id = role_result["id"]
        finally:
            await conn.close()

        # Should succeed
        await permission_service.delete_custom_role(grantor_user, role_id)

        # Verify deleted
        conn = await asyncpg.connect(database_url)
        try:
            row = await conn.fetchrow("SELECT * FROM roles WHERE id = $1", role_id)
            assert row is None
        finally:
            await conn.close()
