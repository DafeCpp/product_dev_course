"""API tests for RBAC v2 endpoints."""
import pytest
from uuid import uuid4
from auth_service.settings import settings


# =============================================================================
# Permissions API Tests
# =============================================================================

class TestListPermissions:
    """Tests for GET /api/v1/permissions."""

    @pytest.mark.asyncio
    async def test_list_permissions_returns_catalog(self, service_client, regular_user_token):
        """List permissions should return the full catalog."""
        response = await service_client.get(
            "/api/v1/permissions",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response.status == 200
        permissions = await response.json()
        assert isinstance(permissions, list)
        assert len(permissions) > 0
        
        # Check structure
        perm = permissions[0]
        assert "id" in perm
        assert "scope_type" in perm
        assert "category" in perm
        assert "description" in perm

    @pytest.mark.asyncio
    async def test_list_permissions_requires_auth(self, service_client):
        """List permissions requires authentication."""
        response = await service_client.get("/api/v1/permissions")
        assert response.status == 401


class TestGetEffectivePermissions:
    """Tests for GET /api/v1/users/{user_id}/effective-permissions."""

    @pytest.mark.asyncio
    async def test_get_own_effective_permissions(self, service_client, regular_user_token):
        """User can view their own effective permissions."""
        # Get user ID from token (we know it from conftest)
        user_id = "550e8400-e29b-41d4-a716-446655440002"
        
        response = await service_client.get(
            f"/api/v1/users/{user_id}/effective-permissions",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response.status == 200
        result = await response.json()
        assert "user_id" in result
        assert "is_superadmin" in result
        assert "system_permissions" in result
        assert "project_permissions" in result

    @pytest.mark.asyncio
    async def test_superadmin_effective_permissions(self, service_client, superadmin_token):
        """Superadmin should have is_superadmin=true."""
        user_id = "550e8400-e29b-41d4-a716-446655440001"
        
        response = await service_client.get(
            f"/api/v1/users/{user_id}/effective-permissions",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert response.status == 200
        result = await response.json()
        assert result["is_superadmin"] is True

    @pytest.mark.asyncio
    async def test_get_effective_permissions_with_project_id(
        self, service_client, project_member_user_token, project_data
    ):
        """Get effective permissions filtered by project."""
        user_id = "550e8400-e29b-41d4-a716-446655440004"
        
        response = await service_client.get(
            f"/api/v1/users/{user_id}/effective-permissions",
            params={"project_id": project_data},
            headers={"Authorization": f"Bearer {project_member_user_token}"},
        )
        assert response.status == 200
        result = await response.json()
        assert "experiments.view" in result["project_permissions"]

    @pytest.mark.asyncio
    async def test_cannot_view_others_permissions_without_permission(
        self, service_client, regular_user_token
    ):
        """User cannot view others permissions without users.list."""
        other_user_id = "550e8400-e29b-41d4-a716-446655440001"
        
        response = await service_client.get(
            f"/api/v1/users/{other_user_id}/effective-permissions",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response.status == 403


# =============================================================================
# System Roles API Tests
# =============================================================================

class TestListSystemRoles:
    """Tests for GET /api/v1/system-roles."""

    @pytest.mark.asyncio
    async def test_list_system_roles_returns_builtin_roles(self, service_client, superadmin_token):
        """List system roles should return built-in roles."""
        response = await service_client.get(
            "/api/v1/system-roles",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert response.status == 200
        roles = await response.json()
        assert isinstance(roles, list)
        
        # Check for built-in roles
        role_names = [r["name"] for r in roles]
        assert "superadmin" in role_names
        assert "admin" in role_names
        assert "operator" in role_names
        assert "auditor" in role_names

    @pytest.mark.asyncio
    async def test_list_system_roles_includes_permissions(self, service_client, superadmin_token):
        """System roles should include their permissions."""
        response = await service_client.get(
            "/api/v1/system-roles",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        roles = await response.json()
        
        admin_role = next(r for r in roles if r["name"] == "admin")
        assert "permissions" in admin_role
        assert "users.list" in admin_role["permissions"]


class TestCreateSystemRole:
    """Tests for POST /api/v1/system-roles."""

    @pytest.mark.asyncio
    async def test_create_custom_system_role(self, service_client, admin_user_token):
        """Admin can create custom system role."""
        response = await service_client.post(
            "/api/v1/system-roles",
            headers={"Authorization": f"Bearer {admin_user_token}"},
            json={
                "name": "Custom Test Role",
                "description": "Test custom role",
                "permissions": ["users.list", "audit.read"],
            },
        )
        assert response.status == 201
        role = await response.json()
        assert role["name"] == "Custom Test Role"
        assert role["is_builtin"] is False
        assert "users.list" in role["permissions"]

    @pytest.mark.asyncio
    async def test_create_system_role_requires_roles_manage(
        self, service_client, regular_user_token
    ):
        """User without roles.manage cannot create system roles."""
        response = await service_client.post(
            "/api/v1/system-roles",
            headers={"Authorization": f"Bearer {regular_user_token}"},
            json={
                "name": "Unauthorized Role",
                "permissions": ["users.list"],
            },
        )
        assert response.status == 403

    @pytest.mark.asyncio
    async def test_cannot_create_duplicate_role_name(
        self, service_client, admin_user_token
    ):
        """Cannot create role with duplicate name."""
        # Create first role
        await service_client.post(
            "/api/v1/system-roles",
            headers={"Authorization": f"Bearer {admin_user_token}"},
            json={
                "name": "Unique Test Role",
                "permissions": ["users.list"],
            },
        )
        
        # Try to create duplicate
        response = await service_client.post(
            "/api/v1/system-roles",
            headers={"Authorization": f"Bearer {admin_user_token}"},
            json={
                "name": "Unique Test Role",
                "permissions": ["audit.read"],
            },
        )
        assert response.status == 400  # or 500 depending on error handling


class TestUpdateSystemRole:
    """Tests for PATCH /api/v1/system-roles/{role_id}."""

    @pytest.mark.asyncio
    async def test_update_custom_system_role(self, service_client, admin_user_token, database_url):
        """Can update custom system role."""
        import asyncpg
        conn = await asyncpg.connect(database_url)
        try:
            # Create custom role
            result = await conn.fetchrow("""
                INSERT INTO roles (name, scope_type, is_builtin, created_by)
                VALUES ('Update Test Role', 'system', false, '550e8400-e29b-41d4-a716-446655440003')
                RETURNING id
            """)
            role_id = str(result["id"])
        finally:
            await conn.close()

        response = await service_client.patch(
            f"/api/v1/system-roles/{role_id}",
            headers={"Authorization": f"Bearer {admin_user_token}"},
            json={
                "name": "Updated Role Name",
                "description": "Updated description",
            },
        )
        assert response.status == 200
        role = await response.json()
        assert role["name"] == "Updated Role Name"

    @pytest.mark.asyncio
    async def test_cannot_update_builtin_role(self, service_client, admin_user_token):
        """Cannot update built-in roles."""
        admin_role_id = "00000000-0000-0000-0000-000000000002"
        
        response = await service_client.patch(
            f"/api/v1/system-roles/{admin_role_id}",
            headers={"Authorization": f"Bearer {admin_user_token}"},
            json={"name": "Hacked Admin"},
        )
        assert response.status == 403


class TestDeleteSystemRole:
    """Tests for DELETE /api/v1/system-roles/{role_id}."""

    @pytest.mark.asyncio
    async def test_delete_custom_system_role(self, service_client, admin_user_token, database_url):
        """Can delete custom system role."""
        import asyncpg
        conn = await asyncpg.connect(database_url)
        try:
            result = await conn.fetchrow("""
                INSERT INTO roles (name, scope_type, is_builtin, created_by)
                VALUES ('Delete Test Role', 'system', false, '550e8400-e29b-41d4-a716-446655440003')
                RETURNING id
            """)
            role_id = str(result["id"])
        finally:
            await conn.close()

        response = await service_client.delete(
            f"/api/v1/system-roles/{role_id}",
            headers={"Authorization": f"Bearer {admin_user_token}"},
        )
        assert response.status == 200

    @pytest.mark.asyncio
    async def test_cannot_delete_builtin_role(self, service_client, admin_user_token):
        """Cannot delete built-in roles."""
        admin_role_id = "00000000-0000-0000-0000-000000000002"
        
        response = await service_client.delete(
            f"/api/v1/system-roles/{admin_role_id}",
            headers={"Authorization": f"Bearer {admin_user_token}"},
        )
        assert response.status == 403


class TestGrantSystemRoleToUser:
    """Tests for POST /api/v1/users/{user_id}/system-roles."""

    @pytest.mark.asyncio
    async def test_grant_system_role(self, service_client, admin_user_token):
        """Admin can grant system role."""
        target_user_id = "550e8400-e29b-41d4-a716-446655440002"  # regularuser
        
        response = await service_client.post(
            f"/api/v1/users/{target_user_id}/system-roles",
            headers={"Authorization": f"Bearer {admin_user_token}"},
            json={"role_id": "00000000-0000-0000-0000-000000000004"},  # auditor
        )
        assert response.status == 201
        result = await response.json()
        assert result["role_id"] == "00000000-0000-0000-0000-000000000004"

    @pytest.mark.asyncio
    async def test_grant_system_role_requires_roles_assign(
        self, service_client, regular_user_token
    ):
        """User without roles.assign cannot grant roles."""
        target_user_id = "550e8400-e29b-41d4-a716-446655440002"
        
        response = await service_client.post(
            f"/api/v1/users/{target_user_id}/system-roles",
            headers={"Authorization": f"Bearer {regular_user_token}"},
            json={"role_id": "00000000-0000-0000-0000-000000000004"},
        )
        assert response.status == 403


class TestRevokeSystemRoleFromUser:
    """Tests for DELETE /api/v1/users/{user_id}/system-roles/{role_id}."""

    @pytest.mark.asyncio
    async def test_revoke_system_role(self, service_client, admin_user_token, database_url):
        """Admin can revoke system role."""
        import asyncpg
        conn = await asyncpg.connect(database_url)
        try:
            # Grant role first
            await conn.execute("""
                INSERT INTO user_system_roles (user_id, role_id, granted_by, granted_at)
                VALUES ('550e8400-e29b-41d4-a716-446655440002',
                        '00000000-0000-0000-0000-000000000004',
                        '550e8400-e29b-41d4-a716-446655440003',
                        now())
            """)
        finally:
            await conn.close()

        response = await service_client.delete(
            "/api/v1/users/550e8400-e29b-41d4-a716-446655440002/system-roles/00000000-0000-0000-0000-000000000004",
            headers={"Authorization": f"Bearer {admin_user_token}"},
        )
        assert response.status == 200


# =============================================================================
# Project Roles API Tests
# =============================================================================

class TestListProjectRoles:
    """Tests for GET /api/v1/projects/{project_id}/roles."""

    @pytest.mark.asyncio
    async def test_list_project_roles_returns_builtin_roles(
        self, service_client, superadmin_token, project_data
    ):
        """List project roles should return built-in project roles."""
        response = await service_client.get(
            f"/api/v1/projects/{project_data}/roles",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert response.status == 200
        roles = await response.json()
        
        role_names = [r["name"] for r in roles]
        assert "owner" in role_names
        assert "editor" in role_names
        assert "viewer" in role_names

    @pytest.mark.asyncio
    async def test_list_project_roles_requires_permission(
        self, service_client, regular_user_token, project_data
    ):
        """User without project.members.view cannot list project roles."""
        response = await service_client.get(
            f"/api/v1/projects/{project_data}/roles",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response.status == 403


class TestCreateProjectRole:
    """Tests for POST /api/v1/projects/{project_id}/roles."""

    @pytest.mark.asyncio
    async def test_create_custom_project_role(
        self, service_client, superadmin_token, project_data
    ):
        """Project owner can create custom project role."""
        response = await service_client.post(
            f"/api/v1/projects/{project_data}/roles",
            headers={"Authorization": f"Bearer {superadmin_token}"},
            json={
                "name": "Custom Project Role",
                "description": "Test custom project role",
                "permissions": ["experiments.view", "runs.create"],
            },
        )
        assert response.status == 201
        role = await response.json()
        assert role["name"] == "Custom Project Role"
        assert role["scope_type"] == "project"


class TestGrantRoleToMember:
    """Tests for POST /api/v1/projects/{project_id}/members/{user_id}/roles."""

    @pytest.mark.asyncio
    async def test_grant_project_role_to_member(
        self, service_client, superadmin_token, project_data
    ):
        """Project owner can grant role to member."""
        target_user_id = "550e8400-e29b-41d4-a716-446655440002"  # regularuser
        
        response = await service_client.post(
            f"/api/v1/projects/{project_data}/members/{target_user_id}/roles",
            headers={"Authorization": f"Bearer {superadmin_token}"},
            json={"role_id": "00000000-0000-0000-0000-000000000012"},  # viewer
        )
        assert response.status == 201
        result = await response.json()
        assert result["role_id"] == "00000000-0000-0000-0000-000000000012"

    @pytest.mark.asyncio
    async def test_grant_project_role_requires_permission(
        self, service_client, regular_user_token, project_data
    ):
        """User without project.members.change_role cannot grant roles."""
        target_user_id = "550e8400-e29b-41d4-a716-446655440002"
        
        response = await service_client.post(
            f"/api/v1/projects/{project_data}/members/{target_user_id}/roles",
            headers={"Authorization": f"Bearer {regular_user_token}"},
            json={"role_id": "00000000-0000-0000-0000-000000000012"},
        )
        assert response.status == 403


class TestRevokeRoleFromMember:
    """Tests for DELETE /api/v1/projects/{project_id}/members/{user_id}/roles/{role_id}."""

    @pytest.mark.asyncio
    async def test_revoke_project_role(
        self, service_client, superadmin_token, project_data, pgsql
    ):
        """Project owner can revoke role from member."""
        import asyncpg
        conn = await asyncpg.connect(database_url)
        try:
            # Grant role first
            await conn.execute("""
                INSERT INTO user_project_roles (user_id, project_id, role_id, granted_by, granted_at)
                VALUES ('550e8400-e29b-41d4-a716-446655440002',
                        $1,
                        '00000000-0000-0000-0000-000000000012',
                        '550e8400-e29b-41d4-a716-446655440001',
                        now())
            """, project_data)
        finally:
            await conn.close()

        response = await service_client.delete(
            f"/api/v1/projects/{project_data}/members/550e8400-e29b-41d4-a716-446655440002/roles/00000000-0000-0000-0000-000000000012",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert response.status == 200

    @pytest.mark.asyncio
    async def test_cannot_revoke_last_owner(
        self, service_client, superadmin_token, project_data
    ):
        """Cannot revoke the last owner of a project."""
        # Superadmin is the owner, try to revoke owner role
        response = await service_client.delete(
            f"/api/v1/projects/{project_data}/members/550e8400-e29b-41d4-a716-446655440001/roles/00000000-0000-0000-0000-000000000010",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert response.status == 409  # Conflict
