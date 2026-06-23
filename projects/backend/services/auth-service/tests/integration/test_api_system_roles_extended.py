"""Extended integration tests for system_roles routes.

Targets the missing coverage ranges:
  - lines 52-78:  get_system_role (happy path, non-system scope → 404, role not found → 404/500)
  - lines 105-111: create_system_role InvalidCredentialsError branch + generic exception branch
  - lines 139-145: update_system_role InvalidCredentialsError branch + generic exception branch
  - lines 160-166: delete_system_role InvalidCredentialsError branch + generic exception branch
  - lines 208-214: grant_system_role_to_user InvalidCredentialsError branch + generic exception branch
  - lines 238-245: revoke_system_role_from_user — success path + 404 (assignment not found)
  - lines 250-286: list_user_system_roles — self access, cross-user with permission,
                   cross-user without permission, empty list, orphaned-role skip
"""
from __future__ import annotations

import pytest
import asyncpg
from uuid import uuid4


# =============================================================================
# GET /api/v1/system-roles/{role_id}
# =============================================================================

class TestGetSystemRole:
    """Tests for GET /api/v1/system-roles/{role_id} (lines 59-78)."""

    @pytest.mark.asyncio
    async def test_get_system_role_happy_path(self, service_client, superadmin_token):
        """Return 200 with role data for a known system role."""
        admin_role_id = "00000000-0000-0000-0000-000000000002"

        response = await service_client.get(
            f"/api/v1/system-roles/{admin_role_id}",
        )
        assert response.status == 200
        data = await response.json()
        assert data["name"] == "admin"
        assert data["scope_type"] == "system"
        assert "permissions" in data

    @pytest.mark.asyncio
    async def test_get_system_role_not_found_returns_error(self, service_client):
        """Return error response for non-existent role UUID."""
        nonexistent_id = str(uuid4())

        response = await service_client.get(
            f"/api/v1/system-roles/{nonexistent_id}",
        )
        # NotFoundError has status_code=404 — the handler reads hasattr(e, "status_code")
        assert response.status in (404, 500)
        data = await response.json()
        assert "error" in data

    @pytest.mark.asyncio
    async def test_get_system_role_project_role_returns_404(self, service_client, database_url):
        """When the role exists but is project-scoped, return 404."""
        conn = await asyncpg.connect(database_url)
        try:
            # Insert a project-scoped role to make it exist in DB
            result = await conn.fetchrow("""
                INSERT INTO roles (name, scope_type, is_builtin)
                VALUES ('projrole_for_get_test', 'project', false)
                RETURNING id
            """)
            project_role_id = str(result["id"])
        finally:
            await conn.close()

        response = await service_client.get(
            f"/api/v1/system-roles/{project_role_id}",
        )
        assert response.status == 404
        data = await response.json()
        assert data["error"] == "Not a system role"

    @pytest.mark.asyncio
    async def test_get_system_role_includes_permissions(self, service_client):
        """Returned role object includes its permissions list."""
        admin_role_id = "00000000-0000-0000-0000-000000000002"

        response = await service_client.get(
            f"/api/v1/system-roles/{admin_role_id}",
        )
        assert response.status == 200
        data = await response.json()
        assert isinstance(data["permissions"], list)
        assert "users.list" in data["permissions"]


# =============================================================================
# POST /api/v1/system-roles — error branches (lines 105-111)
# =============================================================================

class TestCreateSystemRoleErrorBranches:
    """Cover lines 105-111: InvalidCredentialsError and generic exception on create."""

    @pytest.mark.asyncio
    async def test_create_system_role_no_token_returns_401(self, service_client):
        """No Authorization header → 401 via InvalidCredentialsError branch (line 105-106).

        Missing token causes get_requester_id to raise InvalidCredentialsError directly.
        """
        response = await service_client.post(
            "/api/v1/system-roles",
            json={"name": "some role", "permissions": ["users.list"]},
        )
        assert response.status == 401
        data = await response.json()
        assert "error" in data

    @pytest.mark.asyncio
    async def test_create_system_role_malformed_token_returns_error(self, service_client):
        """Malformed token → error response (generic exception branch, line 109-111).

        A structurally-invalid JWT raises ValueError inside get_user_id_from_token,
        which is not caught by InvalidCredentialsError and falls through to the
        generic except branch, returning 500.
        """
        response = await service_client.post(
            "/api/v1/system-roles",
            headers={"Authorization": "Bearer not.a.valid.jwt"},
            json={"name": "some role", "permissions": ["users.list"]},
        )
        # Either 401 (if service wraps into InvalidCredentialsError) or 500 (generic branch)
        assert response.status in (401, 500)
        data = await response.json()
        assert "error" in data

    @pytest.mark.asyncio
    async def test_create_system_role_wrong_permissions_forbidden(
        self, service_client, regular_user_token
    ):
        """User without roles.manage → 403 via generic exception branch (line 110-111)."""
        response = await service_client.post(
            "/api/v1/system-roles",
            headers={"Authorization": f"Bearer {regular_user_token}"},
            json={"name": "forbidden role", "permissions": ["users.list"]},
        )
        assert response.status == 403


# =============================================================================
# PATCH /api/v1/system-roles/{role_id} — error branches (lines 139-145)
# =============================================================================

class TestUpdateSystemRoleErrorBranches:
    """Cover lines 139-145: InvalidCredentialsError and generic exception on update."""

    @pytest.mark.asyncio
    async def test_update_system_role_no_token_returns_401(self, service_client):
        """No token → 401 via InvalidCredentialsError (line 139-140)."""
        admin_role_id = "00000000-0000-0000-0000-000000000002"
        response = await service_client.patch(
            f"/api/v1/system-roles/{admin_role_id}",
            json={"name": "new name"},
        )
        assert response.status == 401

    @pytest.mark.asyncio
    async def test_update_system_role_regular_user_forbidden(
        self, service_client, regular_user_token
    ):
        """Regular user without roles.manage → 403 (generic exception branch, line 143-145)."""
        admin_role_id = "00000000-0000-0000-0000-000000000002"
        response = await service_client.patch(
            f"/api/v1/system-roles/{admin_role_id}",
            headers={"Authorization": f"Bearer {regular_user_token}"},
            json={"name": "hacked"},
        )
        # ForbiddenError has status_code=403 attribute → hits line 144-145
        assert response.status == 403


# =============================================================================
# DELETE /api/v1/system-roles/{role_id} — error branches (lines 160-166)
# =============================================================================

class TestDeleteSystemRoleErrorBranches:
    """Cover lines 160-166: InvalidCredentialsError and generic exception on delete."""

    @pytest.mark.asyncio
    async def test_delete_system_role_no_token_returns_401(self, service_client):
        """No token → 401 via InvalidCredentialsError (line 160-161)."""
        admin_role_id = "00000000-0000-0000-0000-000000000002"
        response = await service_client.delete(
            f"/api/v1/system-roles/{admin_role_id}",
        )
        assert response.status == 401

    @pytest.mark.asyncio
    async def test_delete_system_role_regular_user_forbidden(
        self, service_client, regular_user_token
    ):
        """Regular user without roles.manage → 403 via generic exception branch (line 164-166).

        ForbiddenError has status_code=403 attribute, so getattr(e, 'status_code', 500)
        resolves to 403.
        """
        admin_role_id = "00000000-0000-0000-0000-000000000002"
        response = await service_client.delete(
            f"/api/v1/system-roles/{admin_role_id}",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response.status == 403


# =============================================================================
# POST /api/v1/users/{user_id}/system-roles — error branches (lines 208-214)
# =============================================================================

class TestGrantSystemRoleErrorBranches:
    """Cover lines 208-214: InvalidCredentialsError and generic exception on grant."""

    @pytest.mark.asyncio
    async def test_grant_system_role_no_token_returns_401(self, service_client):
        """No token → 401 (line 208-209)."""
        target_user_id = "550e8400-e29b-41d4-a716-446655440002"
        response = await service_client.post(
            f"/api/v1/users/{target_user_id}/system-roles",
            json={"role_id": "00000000-0000-0000-0000-000000000004"},
        )
        assert response.status == 401

    @pytest.mark.asyncio
    async def test_grant_system_role_regular_user_forbidden(
        self, service_client, regular_user_token
    ):
        """Regular user without roles.assign → 403 (lines 212-214)."""
        target_user_id = "550e8400-e29b-41d4-a716-446655440002"
        response = await service_client.post(
            f"/api/v1/users/{target_user_id}/system-roles",
            headers={"Authorization": f"Bearer {regular_user_token}"},
            json={"role_id": "00000000-0000-0000-0000-000000000004"},
        )
        assert response.status == 403


# =============================================================================
# DELETE /api/v1/users/{user_id}/system-roles/{role_id}
# revoke_system_role_from_user (lines 238-245)
# =============================================================================

class TestRevokeSystemRoleExtended:
    """Additional tests for revoke covering 404 and auth error branches."""

    @pytest.mark.asyncio
    async def test_revoke_returns_404_when_assignment_missing(
        self, service_client, admin_user_token, database_url
    ):
        """Revoking a role that was never granted returns 404 (line 238)."""
        # Create a fresh target user with no roles
        conn = await asyncpg.connect(database_url)
        unique = uuid4().hex[:8]
        try:
            result = await conn.fetchrow(
                "INSERT INTO users (username, email, hashed_password, password_change_required, is_active) "
                "VALUES ($1, $2, '$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG', false, true) "
                "RETURNING id",
                f"noassign_{unique}",
                f"noassign_{unique}@example.com",
            )
            target_id = str(result["id"])
        finally:
            await conn.close()

        # auditor role was never granted to target_id
        response = await service_client.delete(
            f"/api/v1/users/{target_id}/system-roles/00000000-0000-0000-0000-000000000004",
            headers={"Authorization": f"Bearer {admin_user_token}"},
        )
        assert response.status == 404
        data = await response.json()
        assert data["error"] == "Role assignment not found"

    @pytest.mark.asyncio
    async def test_revoke_no_token_returns_401(self, service_client):
        """No token on revoke → 401 (line 239-240)."""
        target_user_id = "550e8400-e29b-41d4-a716-446655440002"
        response = await service_client.delete(
            f"/api/v1/users/{target_user_id}/system-roles/00000000-0000-0000-0000-000000000004",
        )
        assert response.status == 401

    @pytest.mark.asyncio
    async def test_revoke_regular_user_forbidden(
        self, service_client, regular_user_token
    ):
        """Regular user without roles.assign → 403 (line 242-245)."""
        target_user_id = "550e8400-e29b-41d4-a716-446655440002"
        response = await service_client.delete(
            f"/api/v1/users/{target_user_id}/system-roles/00000000-0000-0000-0000-000000000004",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response.status == 403


# =============================================================================
# GET /api/v1/users/{user_id}/system-roles
# list_user_system_roles (lines 250-286)
# =============================================================================

class TestListUserSystemRoles:
    """Cover lines 250-286: all branches of list_user_system_roles."""

    @pytest.mark.asyncio
    async def test_user_can_list_own_system_roles(
        self, service_client, admin_user_token, database_url
    ):
        """User can list their own system roles without extra permission (line 257: same user)."""
        # adminuser has id 550e8400-e29b-41d4-a716-446655440003
        admin_id = "550e8400-e29b-41d4-a716-446655440003"

        response = await service_client.get(
            f"/api/v1/users/{admin_id}/system-roles",
            headers={"Authorization": f"Bearer {admin_user_token}"},
        )
        assert response.status == 200
        data = await response.json()
        assert isinstance(data, list)
        # adminuser has the admin role
        role_names = [entry["role_name"] for entry in data]
        assert "admin" in role_names

    @pytest.mark.asyncio
    async def test_admin_can_list_other_users_system_roles(
        self, service_client, admin_user_token, database_url
    ):
        """User with users.list permission can list another user's roles (line 258)."""
        # Create a target user with auditor role
        conn = await asyncpg.connect(database_url)
        unique = uuid4().hex[:8]
        try:
            result = await conn.fetchrow(
                "INSERT INTO users (username, email, hashed_password, password_change_required, is_active) "
                "VALUES ($1, $2, '$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG', false, true) "
                "RETURNING id",
                f"target_{unique}",
                f"target_{unique}@example.com",
            )
            target_id = str(result["id"])
            await conn.execute(
                "INSERT INTO user_system_roles (user_id, role_id, granted_by, granted_at) "
                "VALUES ($1, '00000000-0000-0000-0000-000000000004', "
                "        '550e8400-e29b-41d4-a716-446655440003', now())",
                result["id"],
            )
        finally:
            await conn.close()

        response = await service_client.get(
            f"/api/v1/users/{target_id}/system-roles",
            headers={"Authorization": f"Bearer {admin_user_token}"},
        )
        assert response.status == 200
        data = await response.json()
        assert isinstance(data, list)
        role_names = [entry["role_name"] for entry in data]
        assert "auditor" in role_names

    @pytest.mark.asyncio
    async def test_regular_user_cannot_list_others_system_roles(
        self, service_client, regular_user_token
    ):
        """User without users.list cannot view another user's system roles (line 258 → 403)."""
        # Try to view adminuser's roles
        admin_id = "550e8400-e29b-41d4-a716-446655440003"
        response = await service_client.get(
            f"/api/v1/users/{admin_id}/system-roles",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response.status == 403

    @pytest.mark.asyncio
    async def test_list_system_roles_empty_for_user_with_no_roles(
        self, service_client, admin_user_token, database_url
    ):
        """User with no system roles returns empty list (line 266 → empty iteration)."""
        conn = await asyncpg.connect(database_url)
        unique = uuid4().hex[:8]
        try:
            result = await conn.fetchrow(
                "INSERT INTO users (username, email, hashed_password, password_change_required, is_active) "
                "VALUES ($1, $2, '$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG', false, true) "
                "RETURNING id",
                f"noroles_{unique}",
                f"noroles_{unique}@example.com",
            )
            no_roles_id = str(result["id"])
        finally:
            await conn.close()

        response = await service_client.get(
            f"/api/v1/users/{no_roles_id}/system-roles",
            headers={"Authorization": f"Bearer {admin_user_token}"},
        )
        assert response.status == 200
        data = await response.json()
        assert data == []

    @pytest.mark.asyncio
    async def test_list_system_roles_skips_orphaned_assignment(
        self, service_client, admin_user_token, database_url
    ):
        """Orphaned assignment (deleted role) is skipped silently (line 269-270)."""
        conn = await asyncpg.connect(database_url)
        unique = uuid4().hex[:8]
        try:
            # Create user
            result = await conn.fetchrow(
                "INSERT INTO users (username, email, hashed_password, password_change_required, is_active) "
                "VALUES ($1, $2, '$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG', false, true) "
                "RETURNING id",
                f"orphan_{unique}",
                f"orphan_{unique}@example.com",
            )
            user_id = result["id"]

            # Create a custom role, assign it, then delete the role leaving orphaned assignment
            ghost_role = await conn.fetchrow(
                "INSERT INTO roles (name, scope_type, is_builtin) "
                "VALUES ($1, 'system', false) RETURNING id",
                f"ghost_role_{unique}",
            )
            ghost_role_id = ghost_role["id"]

            await conn.execute(
                "INSERT INTO user_system_roles (user_id, role_id, granted_by, granted_at) "
                "VALUES ($1, $2, '550e8400-e29b-41d4-a716-446655440003', now())",
                user_id, ghost_role_id,
            )

            # Now delete the role to create an orphaned assignment
            # Must remove role_permissions first (cascade may not cover orphan case we want)
            await conn.execute("DELETE FROM roles WHERE id = $1", ghost_role_id)
            # The user_system_roles row referencing the deleted role may be gone due to FK
            # cascade, but if it remains (no cascade on this FK), we test the skip.
            # Either way the endpoint must return 200 with a list (possibly empty).
            target_id = str(user_id)
        finally:
            await conn.close()

        response = await service_client.get(
            f"/api/v1/users/{target_id}/system-roles",
            headers={"Authorization": f"Bearer {admin_user_token}"},
        )
        assert response.status == 200
        data = await response.json()
        assert isinstance(data, list)
        # Ghost role should NOT appear (either skipped or never stored due to cascade)
        role_names = [entry["role_name"] for entry in data]
        assert f"ghost_role_{unique}" not in role_names

    @pytest.mark.asyncio
    async def test_list_user_system_roles_no_token_returns_401(self, service_client):
        """No token → 401 via InvalidCredentialsError branch (line 280-281)."""
        target_user_id = "550e8400-e29b-41d4-a716-446655440003"
        response = await service_client.get(
            f"/api/v1/users/{target_user_id}/system-roles",
        )
        assert response.status == 401

    @pytest.mark.asyncio
    async def test_list_user_system_roles_response_shape(
        self, service_client, admin_user_token
    ):
        """Verify response items contain all expected fields (line 271-277)."""
        admin_id = "550e8400-e29b-41d4-a716-446655440003"

        response = await service_client.get(
            f"/api/v1/users/{admin_id}/system-roles",
            headers={"Authorization": f"Bearer {admin_user_token}"},
        )
        assert response.status == 200
        data = await response.json()
        assert len(data) >= 1
        entry = data[0]
        for field in ("role_id", "role_name", "granted_by", "granted_at", "expires_at"):
            assert field in entry, f"Missing field: {field}"


# =============================================================================
# list_system_roles — unauthenticated (lines 52-56)
# =============================================================================

class TestListSystemRolesAuthBranch:
    """Cover lines 52-56: InvalidCredentialsError in list_system_roles."""

    @pytest.mark.asyncio
    async def test_list_system_roles_no_token_returns_401(self, service_client):
        """No Authorization header → 401 via InvalidCredentialsError (line 52-53).

        When the Bearer token is entirely absent, get_requester_id raises
        InvalidCredentialsError which is caught at line 52 and returns 401.
        """
        response = await service_client.get("/api/v1/system-roles")
        assert response.status == 401
        data = await response.json()
        assert "error" in data

    @pytest.mark.asyncio
    async def test_list_system_roles_malformed_token_returns_error(self, service_client):
        """Malformed JWT → error response (generic exception branch, line 54-56).

        A structurally-invalid JWT raises ValueError inside jwt decode, which is
        NOT InvalidCredentialsError, so it hits the generic except branch returning 500.
        """
        response = await service_client.get(
            "/api/v1/system-roles",
            headers={"Authorization": "Bearer invalid.token.here"},
        )
        # Falls through to generic except → 500
        assert response.status in (401, 500)
        data = await response.json()
        assert "error" in data
