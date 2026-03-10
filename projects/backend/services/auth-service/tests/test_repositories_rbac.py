"""Unit tests for Permission and Role repositories."""
import pytest
from uuid import uuid4, UUID
import asyncpg

from auth_service.domain.models import Permission, ScopeType
from auth_service.repositories.permissions import PermissionRepository
from auth_service.repositories.roles import RoleRepository


@pytest.fixture
async def permission_repo(pool_with_seed):
    """Create PermissionRepository."""
    return PermissionRepository(pool_with_seed)


@pytest.fixture
async def role_repo(pool_with_seed):
    """Create RoleRepository."""
    return RoleRepository(pool_with_seed)


class TestPermissionRepository:
    """Tests for PermissionRepository."""

    @pytest.mark.asyncio
    async def test_list_all_returns_permissions(self, permission_repo):
        """list_all should return all permissions from catalog."""
        permissions = await permission_repo.list_all()
        assert isinstance(permissions, list)
        assert len(permissions) > 0
        
        # Check structure
        perm = permissions[0]
        assert isinstance(perm, Permission)
        assert perm.id is not None
        assert perm.scope_type in [ScopeType.SYSTEM, ScopeType.PROJECT]
        assert perm.category is not None

    @pytest.mark.asyncio
    async def test_list_by_scope_system(self, permission_repo):
        """list_by_scope should filter by system scope."""
        permissions = await permission_repo.list_by_scope("system")
        assert len(permissions) > 0
        for perm in permissions:
            assert perm.scope_type == ScopeType.SYSTEM

    @pytest.mark.asyncio
    async def test_list_by_scope_project(self, permission_repo):
        """list_by_scope should filter by project scope."""
        permissions = await permission_repo.list_by_scope("project")
        assert len(permissions) > 0
        for perm in permissions:
            assert perm.scope_type == ScopeType.PROJECT

    @pytest.mark.asyncio
    async def test_get_by_ids(self, permission_repo):
        """get_by_ids should return permissions for given IDs."""
        all_perms = await permission_repo.list_all()
        test_ids = [p.id for p in all_perms[:3]]
        
        result = await permission_repo.get_by_ids(test_ids)
        assert len(result) == len(test_ids)
        assert {p.id for p in result} == set(test_ids)

    @pytest.mark.asyncio
    async def test_get_by_ids_empty(self, permission_repo):
        """get_by_ids with empty list should return empty list."""
        result = await permission_repo.get_by_ids([])
        assert result == []

    @pytest.mark.asyncio
    async def test_get_by_ids_nonexistent(self, permission_repo):
        """get_by_ids should return only existing permissions."""
        result = await permission_repo.get_by_ids(["nonexistent.permission"])
        assert result == []


class TestRoleRepository:
    """Tests for RoleRepository."""

    @pytest.mark.asyncio
    async def test_list_system_returns_builtin_roles(self, role_repo):
        """list_system should return built-in system roles."""
        roles = await role_repo.list_system()
        assert len(roles) > 0
        
        role_names = [r.name for r in roles]
        assert "superadmin" in role_names
        assert "admin" in role_names
        assert "operator" in role_names
        assert "auditor" in role_names

    @pytest.mark.asyncio
    async def test_list_system_includes_custom_roles(self, role_repo, database_url):
        """list_system should include custom system roles."""
        from auth_service.settings import settings
        conn = await asyncpg.connect(database_url)
        try:
            await conn.execute("""
                INSERT INTO roles (name, scope_type, is_builtin, description)
                VALUES ('Custom System Role', 'system', false, 'Test')
            """)
        finally:
            await conn.close()

        roles = await role_repo.list_system()
        custom_roles = [r for r in roles if r.name == "Custom System Role"]
        assert len(custom_roles) == 1
        assert custom_roles[0].is_builtin is False

    @pytest.mark.asyncio
    async def test_list_by_project_returns_builtin_templates(self, role_repo, database_url):
        """list_by_project should return built-in project role templates."""
        conn = await asyncpg.connect(database_url)
        try:
            # Create user first (FK constraint)
            user_result = await conn.fetchrow("""
                INSERT INTO users (username, email, hashed_password, is_active)
                VALUES ('projectowner', 'owner@example.com',
                        '$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG',
                        true)
                RETURNING id
            """)
            user_id = user_result["id"]
            
            # Create project
            result = await conn.fetchrow("""
                INSERT INTO projects (name, description, owner_id)
                VALUES ('Test Project', 'Test', $1)
                RETURNING id
            """, user_id)
            project_id = result["id"]
        finally:
            await conn.close()

        roles = await role_repo.list_by_project(project_id)
        role_names = [r.name for r in roles]
        assert "owner" in role_names
        assert "editor" in role_names
        assert "viewer" in role_names

    @pytest.mark.asyncio
    async def test_create_custom_role(self, role_repo, database_url):
        """create should create a custom role."""
        from auth_service.settings import settings
        conn = await asyncpg.connect(database_url)
        try:
            result = await conn.fetchrow("""
                INSERT INTO users (username, email, hashed_password, is_active)
                VALUES ('rolecreator', 'creator@example.com', 
                        '$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG', 
                        true)
                RETURNING id
            """)
            user_id = result["id"]
        finally:
            await conn.close()

        role = await role_repo.create(
            name="Test Custom Role",
            scope_type=ScopeType.SYSTEM,
            description="Test role",
            created_by=user_id,
        )
        
        assert role.name == "Test Custom Role"
        assert role.scope_type == ScopeType.SYSTEM
        assert role.is_builtin is False
        assert role.description == "Test role"

    @pytest.mark.asyncio
    async def test_get_by_id_or_raise(self, role_repo):
        """get_by_id_or_raise should raise NotFoundError for nonexistent role."""
        from auth_service.core.exceptions import NotFoundError
        
        fake_id = uuid4()
        with pytest.raises(NotFoundError, match=f"Role {fake_id} not found"):
            await role_repo.get_by_id_or_raise(fake_id)

    @pytest.mark.asyncio
    async def test_get_by_id_returns_builtin(self, role_repo):
        """get_by_id should return built-in role."""
        admin_role = await role_repo.get_by_id("00000000-0000-0000-0000-000000000002")
        assert admin_role is not None
        assert admin_role.name == "admin"
        assert admin_role.is_builtin is True

    @pytest.mark.asyncio
    async def test_update_custom_role(self, role_repo, database_url):
        """update should modify custom role."""
        from auth_service.settings import settings
        conn = await asyncpg.connect(database_url)
        try:
            result = await conn.fetchrow("""
                INSERT INTO roles (name, scope_type, is_builtin, description)
                VALUES ('Update Test Role', 'system', false, 'Original')
                RETURNING id
            """)
            role_id = result["id"]
        finally:
            await conn.close()

        updated = await role_repo.update(
            role_id,
            name="Updated Role Name",
            description="Updated description",
        )
        
        assert updated.name == "Updated Role Name"
        assert updated.description == "Updated description"

    @pytest.mark.asyncio
    async def test_update_builtin_role(self, role_repo):
        """update should work on built-in roles (no restriction at repo level)."""
        updated = await role_repo.update(
            UUID("00000000-0000-0000-0000-000000000002"),
            description="Updated admin description",
        )
        assert updated.description == "Updated admin description"

    @pytest.mark.asyncio
    async def test_delete_custom_role(self, role_repo, database_url):
        """delete should remove custom role."""
        from auth_service.settings import settings
        conn = await asyncpg.connect(database_url)
        try:
            result = await conn.fetchrow("""
                INSERT INTO roles (name, scope_type, is_builtin)
                VALUES ('Delete Test Role', 'system', false)
                RETURNING id
            """)
            role_id = result["id"]
        finally:
            await conn.close()

        await role_repo.delete(role_id)
        
        # Verify deleted
        role = await role_repo.get_by_id(role_id)
        assert role is None

    @pytest.mark.asyncio
    async def test_delete_builtin_role(self, role_repo, database_url):
        """delete should work on built-in roles (no restriction at repo level)."""
        # Create then delete to avoid breaking other tests
        conn = await asyncpg.connect(database_url)
        try:
            result = await conn.fetchrow("""
                INSERT INTO roles (name, scope_type, is_builtin)
                VALUES ('Temp Builtin', 'system', true)
                RETURNING id
            """)
            role_id = result["id"]
        finally:
            await conn.close()

        await role_repo.delete(role_id)
        role = await role_repo.get_by_id(role_id)
        assert role is None

    @pytest.mark.asyncio
    async def test_get_permissions(self, role_repo):
        """get_permissions should return role's permissions."""
        admin_role = await role_repo.get_by_id("00000000-0000-0000-0000-000000000002")
        perms = await role_repo.get_permissions(admin_role.id)
        
        assert isinstance(perms, list)
        assert len(perms) > 0
        assert "users.list" in perms
        assert "roles.assign" in perms

    @pytest.mark.asyncio
    async def test_get_permissions_empty(self, role_repo, database_url):
        """get_permissions should return empty list for role without permissions."""
        from auth_service.settings import settings
        conn = await asyncpg.connect(database_url)
        try:
            result = await conn.fetchrow("""
                INSERT INTO roles (name, scope_type, is_builtin)
                VALUES ('Empty Role', 'system', false)
                RETURNING id
            """)
            role_id = result["id"]
        finally:
            await conn.close()

        perms = await role_repo.get_permissions(role_id)
        assert perms == []

    @pytest.mark.asyncio
    async def test_set_permissions(self, role_repo, database_url):
        """set_permissions should replace all role permissions."""
        from auth_service.settings import settings
        conn = await asyncpg.connect(database_url)
        try:
            result = await conn.fetchrow("""
                INSERT INTO roles (name, scope_type, is_builtin)
                VALUES ('Set Perms Role', 'system', false)
                RETURNING id
            """)
            role_id = result["id"]
        finally:
            await conn.close()

        # Set initial permissions
        await role_repo.set_permissions(role_id, ["users.list", "audit.read"])
        perms = await role_repo.get_permissions(role_id)
        assert set(perms) == {"users.list", "audit.read"}

        # Replace permissions
        await role_repo.set_permissions(role_id, ["scripts.execute"])
        perms = await role_repo.get_permissions(role_id)
        assert perms == ["scripts.execute"]
        assert "users.list" not in perms

    @pytest.mark.asyncio
    async def test_set_permissions_empty_list(self, role_repo, database_url):
        """set_permissions with empty list should remove all permissions."""
        from auth_service.settings import settings
        conn = await asyncpg.connect(database_url)
        try:
            result = await conn.fetchrow("""
                INSERT INTO roles (name, scope_type, is_builtin)
                VALUES ('Clear Perms Role', 'system', false)
                RETURNING id
            """)
            role_id = result["id"]
            
            # Add some permissions first
            await conn.execute("""
                INSERT INTO role_permissions (role_id, permission_id)
                VALUES ($1, 'users.list')
            """, role_id)
        finally:
            await conn.close()

        # Clear permissions
        await role_repo.set_permissions(role_id, [])
        perms = await role_repo.get_permissions(role_id)
        assert perms == []
