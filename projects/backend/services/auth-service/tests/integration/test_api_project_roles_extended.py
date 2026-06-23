"""Extended integration tests for project_roles.py routes.

Target: raise coverage of auth_service.api.routes.project_roles from 44% to ≥80%.
Covers all seven handlers including auth-failure (401), cross-project 404,
permission-denied (403), exception paths (500-via-status_code), orphan-skip
in list_member_roles, expires_at parsing branch, and revoke-not-found (404).
"""
from __future__ import annotations

import pytest
import asyncpg
from uuid import uuid4


# ---------------------------------------------------------------------------
# Shared IDs (mirrors conftest.py seed data)
# ---------------------------------------------------------------------------

SUPERADMIN_USER_ID = "550e8400-e29b-41d4-a716-446655440001"
REGULAR_USER_ID = "550e8400-e29b-41d4-a716-446655440002"
ADMIN_USER_ID = "550e8400-e29b-41d4-a716-446655440003"
MEMBER_USER_ID = "550e8400-e29b-41d4-a716-446655440004"

OWNER_ROLE_ID = "00000000-0000-0000-0000-000000000010"
EDITOR_ROLE_ID = "00000000-0000-0000-0000-000000000011"
VIEWER_ROLE_ID = "00000000-0000-0000-0000-000000000012"
ADMIN_SYSTEM_ROLE_ID = "00000000-0000-0000-0000-000000000002"

HASHED_PASSWORD = "$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG"
PLAIN_PASSWORD = "admin123"


# ---------------------------------------------------------------------------
# Module-local helper fixtures
# ---------------------------------------------------------------------------

@pytest.fixture
async def viewer_user_token(service_client, database_url, project_data):
    """Create a viewer-only member and return their token.

    The viewer role has project.members.view but NOT project.roles.manage
    and NOT project.members.change_role, which is useful for permission-denied paths.
    """
    conn = await asyncpg.connect(database_url)
    try:
        await conn.execute(
            """
            INSERT INTO users (id, username, email, hashed_password,
                               password_change_required, is_active)
            VALUES ('660e8400-e29b-41d4-a716-000000000001', 'vieweruser',
                    'viewer@example.com', $1, false, true)
            ON CONFLICT (username) DO UPDATE SET is_active = true
            """,
            HASHED_PASSWORD,
        )
        await conn.execute(
            """
            INSERT INTO user_project_roles (user_id, project_id, role_id,
                                            granted_by, granted_at)
            VALUES ('660e8400-e29b-41d4-a716-000000000001', $1,
                    $2,
                    $3, now())
            ON CONFLICT (user_id, project_id, role_id) DO NOTHING
            """,
            project_data,
            VIEWER_ROLE_ID,
            SUPERADMIN_USER_ID,
        )
    finally:
        await conn.close()

    login_resp = await service_client.post(
        "/auth/login",
        json={"username": "vieweruser", "password": PLAIN_PASSWORD},
    )
    assert login_resp.status == 200
    return (await login_resp.json())["access_token"]


@pytest.fixture
async def second_project_id(database_url, superadmin_token):
    """Create a second project with a different UUID; return its string ID."""
    conn = await asyncpg.connect(database_url)
    try:
        result = await conn.fetchrow(
            """
            INSERT INTO projects (id, name, description, owner_id)
            VALUES ('770e8400-e29b-41d4-a716-446655440020',
                    'Second Project', 'Another project',
                    $1)
            ON CONFLICT (id) DO UPDATE SET name = EXCLUDED.name
            RETURNING id
            """,
            SUPERADMIN_USER_ID,
        )
        return str(result["id"])
    finally:
        await conn.close()


@pytest.fixture
async def custom_project_role_id(database_url, project_data):
    """Seed a custom (non-builtin) project role and return its UUID string."""
    conn = await asyncpg.connect(database_url)
    try:
        result = await conn.fetchrow(
            """
            INSERT INTO roles (name, scope_type, project_id, is_builtin, created_by)
            VALUES ('Custom Viewer Extended', 'project', $1, false,
                    $2)
            ON CONFLICT DO NOTHING
            RETURNING id
            """,
            project_data,
            SUPERADMIN_USER_ID,
        )
        if result is None:
            row = await conn.fetchrow(
                "SELECT id FROM roles WHERE name = 'Custom Viewer Extended'"
            )
            return str(row["id"])
        return str(result["id"])
    finally:
        await conn.close()


# ===========================================================================
# 1. list_project_roles
# ===========================================================================

class TestListProjectRolesExtended:
    """GET /api/v1/projects/{project_id}/roles — covers lines 55, 60."""

    @pytest.mark.asyncio
    async def test_returns_401_without_token(self, service_client, project_data):
        """No Authorization header → 401 (InvalidCredentialsError path, line 55)."""
        response = await service_client.get(
            f"/api/v1/projects/{project_data}/roles",
        )
        assert response.status == 401

    @pytest.mark.asyncio
    async def test_returns_500_with_invalid_token(self, service_client, project_data):
        """Garbage token raises ValueError (not InvalidCredentialsError) → generic except
        path fires (line 60). The JWT library throws DecodeError which becomes ValueError
        in get_user_id_from_token, caught by the bare except and returned as 500."""
        response = await service_client.get(
            f"/api/v1/projects/{project_data}/roles",
            headers={"Authorization": "Bearer not-a-valid-jwt"},
        )
        # The invalid JWT propagates as a generic exception → 500 (line 60)
        assert response.status == 500

    @pytest.mark.asyncio
    async def test_returns_403_without_view_permission(
        self, service_client, regular_user_token, project_data
    ):
        """Regular user not in project → 403 (ensure_permission raises ForbiddenError)."""
        response = await service_client.get(
            f"/api/v1/projects/{project_data}/roles",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response.status == 403

    @pytest.mark.asyncio
    async def test_viewer_can_list_project_roles(
        self, service_client, viewer_user_token, project_data
    ):
        """Viewer member (has project.members.view) can list roles — happy path."""
        response = await service_client.get(
            f"/api/v1/projects/{project_data}/roles",
            headers={"Authorization": f"Bearer {viewer_user_token}"},
        )
        assert response.status == 200
        roles = await response.json()
        assert isinstance(roles, list)
        role_names = [r["name"] for r in roles]
        assert "owner" in role_names
        assert "editor" in role_names
        assert "viewer" in role_names


# ===========================================================================
# 2. get_project_role
# ===========================================================================

class TestGetProjectRoleExtended:
    """GET /api/v1/projects/{project_id}/roles/{role_id} — covers lines 65-92."""

    @pytest.mark.asyncio
    async def test_happy_path_returns_role(
        self, service_client, superadmin_token, project_data, custom_project_role_id
    ):
        """Happy path: superadmin can fetch a custom project-scoped role.

        Built-in project roles (owner/editor/viewer) have project_id=NULL in the DB,
        so the handler's cross-project check would return 404 for them.
        A custom role seeded with the real project_id works correctly.
        """
        response = await service_client.get(
            f"/api/v1/projects/{project_data}/roles/{custom_project_role_id}",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert response.status == 200
        body = await response.json()
        assert body["id"] == custom_project_role_id
        assert body["scope_type"] == "project"
        assert "permissions" in body

    @pytest.mark.asyncio
    async def test_returns_404_for_role_in_different_project(
        self, service_client, superadmin_token, project_data, second_project_id, database_url
    ):
        """Role that belongs to a different project → 404 (line 81)."""
        # Create a custom role scoped to second_project_id
        conn = await asyncpg.connect(database_url)
        try:
            result = await conn.fetchrow(
                """
                INSERT INTO roles (name, scope_type, project_id, is_builtin, created_by)
                VALUES ('OtherProjectRole', 'project', $1, false, $2)
                RETURNING id
                """,
                second_project_id,
                SUPERADMIN_USER_ID,
            )
            other_role_id = str(result["id"])
        finally:
            await conn.close()

        response = await service_client.get(
            f"/api/v1/projects/{project_data}/roles/{other_role_id}",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert response.status == 404
        body = await response.json()
        assert "not found" in body["error"].lower()

    @pytest.mark.asyncio
    async def test_returns_404_for_nonexistent_role(
        self, service_client, superadmin_token, project_data
    ):
        """Role UUID that doesn't exist at all → 404 via NotFoundError (line 90-91)."""
        nonexistent_role_id = str(uuid4())
        response = await service_client.get(
            f"/api/v1/projects/{project_data}/roles/{nonexistent_role_id}",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert response.status == 404

    @pytest.mark.asyncio
    async def test_returns_401_without_token(self, service_client, project_data):
        """No token → 401 (line 87)."""
        response = await service_client.get(
            f"/api/v1/projects/{project_data}/roles/{VIEWER_ROLE_ID}",
        )
        assert response.status == 401

    @pytest.mark.asyncio
    async def test_returns_403_without_view_permission(
        self, service_client, regular_user_token, project_data
    ):
        """Non-member → 403 via ForbiddenError (line 90-91, status_code branch)."""
        response = await service_client.get(
            f"/api/v1/projects/{project_data}/roles/{VIEWER_ROLE_ID}",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response.status == 403

    @pytest.mark.asyncio
    async def test_viewer_can_get_custom_project_role(
        self, service_client, viewer_user_token, project_data, custom_project_role_id
    ):
        """Viewer (has project.members.view) can fetch a custom role in their project.

        Built-in roles have project_id=NULL so the handler returns 404 for them;
        custom roles seeded with the real project_id return 200.
        """
        response = await service_client.get(
            f"/api/v1/projects/{project_data}/roles/{custom_project_role_id}",
            headers={"Authorization": f"Bearer {viewer_user_token}"},
        )
        assert response.status == 200
        body = await response.json()
        assert body["id"] == custom_project_role_id


# ===========================================================================
# 3. create_project_role
# ===========================================================================

class TestCreateProjectRoleExtended:
    """POST /api/v1/projects/{project_id}/roles — covers lines 121-127."""

    @pytest.mark.asyncio
    async def test_returns_401_without_token(self, service_client, project_data):
        """No token → 401 (line 122)."""
        response = await service_client.post(
            f"/api/v1/projects/{project_data}/roles",
            json={"name": "Noauth Role", "permissions": []},
        )
        assert response.status == 401

    @pytest.mark.asyncio
    async def test_returns_403_without_roles_manage(
        self, service_client, viewer_user_token, project_data
    ):
        """Viewer has project.members.view but NOT project.roles.manage → 403 (line 125-126)."""
        response = await service_client.post(
            f"/api/v1/projects/{project_data}/roles",
            headers={"Authorization": f"Bearer {viewer_user_token}"},
            json={
                "name": "Viewer Cannot Create",
                "permissions": ["experiments.view"],
            },
        )
        assert response.status == 403

    @pytest.mark.asyncio
    async def test_returns_409_on_duplicate_name(
        self, service_client, superadmin_token, project_data
    ):
        """Duplicate role name → 409 ConflictError (line 125-126, status_code branch)."""
        role_name = f"DuplicateRole-{uuid4().hex[:8]}"
        # First creation succeeds
        r1 = await service_client.post(
            f"/api/v1/projects/{project_data}/roles",
            headers={"Authorization": f"Bearer {superadmin_token}"},
            json={"name": role_name, "permissions": ["experiments.view"]},
        )
        assert r1.status == 201

        # Second creation with same name → 409
        r2 = await service_client.post(
            f"/api/v1/projects/{project_data}/roles",
            headers={"Authorization": f"Bearer {superadmin_token}"},
            json={"name": role_name, "permissions": ["experiments.view"]},
        )
        assert r2.status == 409


# ===========================================================================
# 4. update_project_role
# ===========================================================================

class TestUpdateProjectRoleExtended:
    """PATCH /api/v1/projects/{project_id}/roles/{role_id} — covers lines 135-167."""

    @pytest.mark.asyncio
    async def test_happy_path_update_custom_role(
        self, service_client, superadmin_token, project_data, custom_project_role_id
    ):
        """Superadmin can update a custom project role (happy path, line 135-160)."""
        response = await service_client.patch(
            f"/api/v1/projects/{project_data}/roles/{custom_project_role_id}",
            headers={"Authorization": f"Bearer {superadmin_token}"},
            json={
                "name": "Updated Custom Viewer",
                "description": "Updated description",
            },
        )
        assert response.status == 200
        body = await response.json()
        assert body["name"] == "Updated Custom Viewer"

    @pytest.mark.asyncio
    async def test_returns_404_when_role_belongs_to_other_project(
        self, service_client, superadmin_token, project_data, second_project_id, database_url
    ):
        """Cross-project role update → 404 (line 147)."""
        conn = await asyncpg.connect(database_url)
        try:
            result = await conn.fetchrow(
                """
                INSERT INTO roles (name, scope_type, project_id, is_builtin, created_by)
                VALUES ('CrossProjectRoleUpd', 'project', $1, false, $2)
                RETURNING id
                """,
                second_project_id,
                SUPERADMIN_USER_ID,
            )
            cross_role_id = str(result["id"])
        finally:
            await conn.close()

        response = await service_client.patch(
            f"/api/v1/projects/{project_data}/roles/{cross_role_id}",
            headers={"Authorization": f"Bearer {superadmin_token}"},
            json={"name": "Should Not Update"},
        )
        assert response.status == 404
        body = await response.json()
        assert "not found" in body["error"].lower()

    @pytest.mark.asyncio
    async def test_returns_401_without_token(
        self, service_client, project_data, custom_project_role_id
    ):
        """No token → 401 (line 162)."""
        response = await service_client.patch(
            f"/api/v1/projects/{project_data}/roles/{custom_project_role_id}",
            json={"name": "Noauth"},
        )
        assert response.status == 401

    @pytest.mark.asyncio
    async def test_returns_403_without_roles_manage(
        self, service_client, viewer_user_token, project_data, custom_project_role_id
    ):
        """Viewer has no project.roles.manage → 403 (line 165-166)."""
        response = await service_client.patch(
            f"/api/v1/projects/{project_data}/roles/{custom_project_role_id}",
            headers={"Authorization": f"Bearer {viewer_user_token}"},
            json={"name": "Viewer Cannot Update"},
        )
        assert response.status == 403

    @pytest.mark.asyncio
    async def test_returns_404_for_builtin_role_via_project_endpoint(
        self, service_client, superadmin_token, project_data
    ):
        """Built-in project roles have project_id=NULL, so the cross-project check
        (line 146-147) fires before the is_builtin check, returning 404.
        This exercises the 404 branch of update_project_role (line 147).
        """
        response = await service_client.patch(
            f"/api/v1/projects/{project_data}/roles/{EDITOR_ROLE_ID}",
            headers={"Authorization": f"Bearer {superadmin_token}"},
            json={"name": "Hacked Editor"},
        )
        # project_id=NULL on built-in roles → cross-project 404 fires first
        assert response.status == 404

    @pytest.mark.asyncio
    async def test_returns_403_updating_project_builtin_role_with_matching_project_id(
        self, service_client, superadmin_token, project_data, database_url
    ):
        """A builtin role seeded with the actual project_id passes the cross-project check
        but then hits the is_builtin guard in update_custom_role → 403 (line 165-166)."""
        conn = await asyncpg.connect(database_url)
        try:
            result = await conn.fetchrow(
                """
                INSERT INTO roles (name, scope_type, project_id, is_builtin, created_by)
                VALUES ('BuiltinProjectRole', 'project', $1, true, $2)
                RETURNING id
                """,
                project_data,
                SUPERADMIN_USER_ID,
            )
            builtin_role_id = str(result["id"])
        finally:
            await conn.close()

        response = await service_client.patch(
            f"/api/v1/projects/{project_data}/roles/{builtin_role_id}",
            headers={"Authorization": f"Bearer {superadmin_token}"},
            json={"name": "Cannot Update Builtin"},
        )
        assert response.status == 403

    @pytest.mark.asyncio
    async def test_returns_404_for_nonexistent_role(
        self, service_client, superadmin_token, project_data
    ):
        """Non-existent role_id → 404 NotFoundError (line 165-166)."""
        response = await service_client.patch(
            f"/api/v1/projects/{project_data}/roles/{uuid4()}",
            headers={"Authorization": f"Bearer {superadmin_token}"},
            json={"name": "Ghost"},
        )
        assert response.status == 404


# ===========================================================================
# 5. delete_project_role
# ===========================================================================

class TestDeleteProjectRoleExtended:
    """DELETE /api/v1/projects/{project_id}/roles/{role_id} — covers lines 175-194."""

    @pytest.fixture
    async def disposable_role_id(self, database_url, project_data):
        """Create a fresh custom role that can be safely deleted in this test."""
        conn = await asyncpg.connect(database_url)
        try:
            result = await conn.fetchrow(
                """
                INSERT INTO roles (name, scope_type, project_id, is_builtin, created_by)
                VALUES ($1, 'project', $2, false, $3)
                RETURNING id
                """,
                f"DisposableRole-{uuid4().hex[:8]}",
                project_data,
                SUPERADMIN_USER_ID,
            )
            return str(result["id"])
        finally:
            await conn.close()

    @pytest.mark.asyncio
    async def test_happy_path_deletes_custom_role(
        self, service_client, superadmin_token, project_data, disposable_role_id
    ):
        """Happy path: superadmin deletes a custom project role → 204 (lines 175-187)."""
        response = await service_client.delete(
            f"/api/v1/projects/{project_data}/roles/{disposable_role_id}",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert response.status == 204

    @pytest.mark.asyncio
    async def test_returns_404_when_role_in_different_project(
        self, service_client, superadmin_token, project_data, second_project_id, database_url
    ):
        """Role from another project → 404 (line 184)."""
        conn = await asyncpg.connect(database_url)
        try:
            result = await conn.fetchrow(
                """
                INSERT INTO roles (name, scope_type, project_id, is_builtin, created_by)
                VALUES ('CrossProjectRoleDel', 'project', $1, false, $2)
                RETURNING id
                """,
                second_project_id,
                SUPERADMIN_USER_ID,
            )
            cross_role_id = str(result["id"])
        finally:
            await conn.close()

        response = await service_client.delete(
            f"/api/v1/projects/{project_data}/roles/{cross_role_id}",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert response.status == 404
        body = await response.json()
        assert "not found" in body["error"].lower()

    @pytest.mark.asyncio
    async def test_returns_401_without_token(
        self, service_client, project_data, disposable_role_id
    ):
        """No token → 401 (line 189)."""
        response = await service_client.delete(
            f"/api/v1/projects/{project_data}/roles/{disposable_role_id}",
        )
        assert response.status == 401

    @pytest.mark.asyncio
    async def test_returns_403_without_roles_manage(
        self, service_client, viewer_user_token, project_data, disposable_role_id
    ):
        """Viewer lacks project.roles.manage → 403 (line 192-193)."""
        response = await service_client.delete(
            f"/api/v1/projects/{project_data}/roles/{disposable_role_id}",
            headers={"Authorization": f"Bearer {viewer_user_token}"},
        )
        assert response.status == 403

    @pytest.mark.asyncio
    async def test_returns_404_for_builtin_role_via_project_endpoint(
        self, service_client, superadmin_token, project_data
    ):
        """Built-in project roles have project_id=NULL, so the cross-project check
        (line 183-184) fires before delete_custom_role, returning 404.
        This exercises the 404 branch inside delete_project_role (line 184).
        """
        response = await service_client.delete(
            f"/api/v1/projects/{project_data}/roles/{VIEWER_ROLE_ID}",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        # project_id=NULL on built-in roles → cross-project 404 fires first
        assert response.status == 404

    @pytest.mark.asyncio
    async def test_returns_403_deleting_project_builtin_role_with_matching_project_id(
        self, service_client, superadmin_token, project_data, database_url
    ):
        """A builtin role seeded with the actual project_id passes the cross-project check
        but then hits the is_builtin guard in delete_custom_role → 403 (line 192-193)."""
        conn = await asyncpg.connect(database_url)
        try:
            result = await conn.fetchrow(
                """
                INSERT INTO roles (name, scope_type, project_id, is_builtin, created_by)
                VALUES ('BuiltinProjectRoleDel', 'project', $1, true, $2)
                RETURNING id
                """,
                project_data,
                SUPERADMIN_USER_ID,
            )
            builtin_role_id = str(result["id"])
        finally:
            await conn.close()

        response = await service_client.delete(
            f"/api/v1/projects/{project_data}/roles/{builtin_role_id}",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert response.status == 403

    @pytest.mark.asyncio
    async def test_returns_404_for_nonexistent_role(
        self, service_client, superadmin_token, project_data
    ):
        """Non-existent role → 404 NotFoundError (line 192-193)."""
        response = await service_client.delete(
            f"/api/v1/projects/{project_data}/roles/{uuid4()}",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert response.status == 404


# ===========================================================================
# 6. list_member_roles
# ===========================================================================

class TestListMemberRolesExtended:
    """GET /api/v1/projects/{project_id}/members/{user_id}/roles — covers lines 206-242."""

    @pytest.mark.asyncio
    async def test_happy_path_returns_member_roles(
        self, service_client, superadmin_token, project_data, database_url
    ):
        """Superadmin can list roles for an existing project member (lines 206-235)."""
        conn = await asyncpg.connect(database_url)
        try:
            await conn.execute(
                """
                INSERT INTO users (id, username, email, hashed_password,
                                   password_change_required, is_active)
                VALUES ($1, 'memberforlist', 'memberforlist@example.com',
                        $2, false, true)
                ON CONFLICT (id) DO NOTHING
                """,
                MEMBER_USER_ID,
                HASHED_PASSWORD,
            )
            await conn.execute(
                """
                INSERT INTO user_project_roles (user_id, project_id, role_id,
                                                granted_by, granted_at)
                VALUES ($1, $2, $3, $4, now())
                ON CONFLICT (user_id, project_id, role_id) DO NOTHING
                """,
                MEMBER_USER_ID,
                project_data,
                EDITOR_ROLE_ID,
                SUPERADMIN_USER_ID,
            )
        finally:
            await conn.close()

        response = await service_client.get(
            f"/api/v1/projects/{project_data}/members/{MEMBER_USER_ID}/roles",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert response.status == 200
        body = await response.json()
        assert isinstance(body, list)
        role_ids = [item["role_id"] for item in body]
        assert EDITOR_ROLE_ID in role_ids

    @pytest.mark.asyncio
    async def test_returns_empty_list_for_user_with_no_assignments(
        self, service_client, superadmin_token, project_data, database_url
    ):
        """User with no project roles → empty list (line 235)."""
        user_id = "660e8400-e29b-41d4-a716-000000000099"
        conn = await asyncpg.connect(database_url)
        try:
            await conn.execute(
                """
                INSERT INTO users (id, username, email, hashed_password,
                                   password_change_required, is_active)
                VALUES ($1, 'noroleuser', 'norolesuser@example.com',
                        $2, false, true)
                ON CONFLICT (id) DO NOTHING
                """,
                user_id,
                HASHED_PASSWORD,
            )
        finally:
            await conn.close()

        response = await service_client.get(
            f"/api/v1/projects/{project_data}/members/{user_id}/roles",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert response.status == 200
        body = await response.json()
        assert body == []

    @pytest.mark.asyncio
    async def test_orphaned_role_is_skipped(
        self, service_client, superadmin_token, project_data, database_url
    ):
        """Assignment referencing a deleted role is silently skipped (line 225-226).

        We insert a user_project_roles row pointing at a non-existent role_id
        and verify the endpoint still returns 200 (without that entry in the list).
        """
        user_id = "660e8400-e29b-41d4-a716-000000000088"
        ghost_role_id = str(uuid4())

        conn = await asyncpg.connect(database_url)
        try:
            await conn.execute(
                """
                INSERT INTO users (id, username, email, hashed_password,
                                   password_change_required, is_active)
                VALUES ($1, 'orphanuser', 'orphan@example.com',
                        $2, false, true)
                ON CONFLICT (id) DO NOTHING
                """,
                user_id,
                HASHED_PASSWORD,
            )
            # Bypass FK by inserting into user_project_roles directly is not
            # allowed if there is a FK constraint.  Insert a real role first,
            # delete it afterwards, leaving an orphan assignment.
            await conn.execute(
                """
                INSERT INTO roles (id, name, scope_type, project_id, is_builtin, created_by)
                VALUES ($1, 'GhostRole', 'project', $2, false, $3)
                """,
                ghost_role_id,
                project_data,
                SUPERADMIN_USER_ID,
            )
            await conn.execute(
                """
                INSERT INTO user_project_roles (user_id, project_id, role_id,
                                                granted_by, granted_at)
                VALUES ($1, $2, $3, $4, now())
                ON CONFLICT (user_id, project_id, role_id) DO NOTHING
                """,
                user_id,
                project_data,
                ghost_role_id,
                SUPERADMIN_USER_ID,
            )
            # Delete the role to create an orphan assignment
            await conn.execute("DELETE FROM roles WHERE id = $1", ghost_role_id)
        finally:
            await conn.close()

        response = await service_client.get(
            f"/api/v1/projects/{project_data}/members/{user_id}/roles",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert response.status == 200
        body = await response.json()
        # The orphan entry must not be present
        role_ids = [item["role_id"] for item in body]
        assert ghost_role_id not in role_ids

    @pytest.mark.asyncio
    async def test_returns_401_without_token(self, service_client, project_data):
        """No token → 401 (line 237)."""
        response = await service_client.get(
            f"/api/v1/projects/{project_data}/members/{MEMBER_USER_ID}/roles",
        )
        assert response.status == 401

    @pytest.mark.asyncio
    async def test_returns_403_without_view_permission(
        self, service_client, regular_user_token, project_data
    ):
        """Non-member regular user → 403 (line 240-241, status_code branch)."""
        response = await service_client.get(
            f"/api/v1/projects/{project_data}/members/{MEMBER_USER_ID}/roles",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response.status == 403

    @pytest.mark.asyncio
    async def test_includes_expires_at_when_set(
        self, service_client, superadmin_token, project_data, database_url
    ):
        """Assignments with expires_at must have it in the response (not None)."""
        user_id = "660e8400-e29b-41d4-a716-000000000077"
        conn = await asyncpg.connect(database_url)
        try:
            await conn.execute(
                """
                INSERT INTO users (id, username, email, hashed_password,
                                   password_change_required, is_active)
                VALUES ($1, 'expiresuser', 'expires@example.com',
                        $2, false, true)
                ON CONFLICT (id) DO NOTHING
                """,
                user_id,
                HASHED_PASSWORD,
            )
            await conn.execute(
                """
                INSERT INTO user_project_roles (user_id, project_id, role_id,
                                                granted_by, granted_at, expires_at)
                VALUES ($1, $2, $3, $4, now(), now() + interval '30 days')
                ON CONFLICT (user_id, project_id, role_id)
                DO UPDATE SET expires_at = now() + interval '30 days'
                """,
                user_id,
                project_data,
                VIEWER_ROLE_ID,
                SUPERADMIN_USER_ID,
            )
        finally:
            await conn.close()

        response = await service_client.get(
            f"/api/v1/projects/{project_data}/members/{user_id}/roles",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert response.status == 200
        body = await response.json()
        viewer_entries = [e for e in body if e["role_id"] == VIEWER_ROLE_ID]
        assert len(viewer_entries) >= 1
        assert viewer_entries[0]["expires_at"] is not None


# ===========================================================================
# 7. grant_role_to_member
# ===========================================================================

class TestGrantRoleToMemberExtended:
    """POST /api/v1/projects/{project_id}/members/{user_id}/roles — covers 265-266, 284, 289."""

    @pytest.fixture
    async def grantee_user_id(self, database_url):
        """Ensure a user exists to be granted a role."""
        uid = "660e8400-e29b-41d4-a716-000000000010"
        conn = await asyncpg.connect(database_url)
        try:
            await conn.execute(
                """
                INSERT INTO users (id, username, email, hashed_password,
                                   password_change_required, is_active)
                VALUES ($1, 'granteeuser', 'grantee@example.com',
                        $2, false, true)
                ON CONFLICT (id) DO NOTHING
                """,
                uid,
                HASHED_PASSWORD,
            )
        finally:
            await conn.close()
        return uid

    @pytest.mark.asyncio
    async def test_happy_path_without_expires_at(
        self, service_client, superadmin_token, project_data, grantee_user_id
    ):
        """Grant without expires_at → 201, expires_at is None in response (skips line 264-266)."""
        response = await service_client.post(
            f"/api/v1/projects/{project_data}/members/{grantee_user_id}/roles",
            headers={"Authorization": f"Bearer {superadmin_token}"},
            json={"role_id": VIEWER_ROLE_ID},
        )
        assert response.status == 201
        body = await response.json()
        assert body["role_id"] == VIEWER_ROLE_ID
        assert body["expires_at"] is None

    @pytest.mark.asyncio
    async def test_happy_path_with_expires_at(
        self, service_client, superadmin_token, project_data, grantee_user_id, database_url
    ):
        """Grant with ISO-8601 expires_at → 201, expires_at branch executed (lines 264-266).

        We delete any existing assignment first to avoid CONFLICT.
        """
        conn = await asyncpg.connect(database_url)
        try:
            await conn.execute(
                """
                DELETE FROM user_project_roles
                WHERE user_id = $1 AND project_id = $2 AND role_id = $3
                """,
                grantee_user_id,
                project_data,
                EDITOR_ROLE_ID,
            )
        finally:
            await conn.close()

        response = await service_client.post(
            f"/api/v1/projects/{project_data}/members/{grantee_user_id}/roles",
            headers={"Authorization": f"Bearer {superadmin_token}"},
            json={
                "role_id": EDITOR_ROLE_ID,
                "expires_at": "2099-12-31T23:59:59Z",
            },
        )
        assert response.status == 201
        body = await response.json()
        assert body["role_id"] == EDITOR_ROLE_ID
        # expires_at was parsed and stored (the response serialises it back)
        # We only verify the endpoint succeeds and returns the correct role.

    @pytest.mark.asyncio
    async def test_returns_401_without_token(
        self, service_client, project_data, grantee_user_id
    ):
        """No token → 401 (line 284)."""
        response = await service_client.post(
            f"/api/v1/projects/{project_data}/members/{grantee_user_id}/roles",
            json={"role_id": VIEWER_ROLE_ID},
        )
        assert response.status == 401

    @pytest.mark.asyncio
    async def test_returns_403_without_change_role_permission(
        self, service_client, viewer_user_token, project_data, grantee_user_id
    ):
        """Viewer lacks project.members.change_role → 403 (line 288-289)."""
        response = await service_client.post(
            f"/api/v1/projects/{project_data}/members/{grantee_user_id}/roles",
            headers={"Authorization": f"Bearer {viewer_user_token}"},
            json={"role_id": VIEWER_ROLE_ID},
        )
        assert response.status == 403

    @pytest.mark.asyncio
    async def test_returns_403_assigning_system_role_as_project_role(
        self, service_client, superadmin_token, project_data, grantee_user_id
    ):
        """Attempting to assign a system-scoped role as a project role → 403 (line 288-289)."""
        response = await service_client.post(
            f"/api/v1/projects/{project_data}/members/{grantee_user_id}/roles",
            headers={"Authorization": f"Bearer {superadmin_token}"},
            json={"role_id": ADMIN_SYSTEM_ROLE_ID},
        )
        assert response.status == 403


# ===========================================================================
# 8. revoke_role_from_member
# ===========================================================================

class TestRevokeRoleFromMemberExtended:
    """DELETE /api/v1/projects/{project_id}/members/{user_id}/roles/{role_id}
    Covers lines 315, 317, 322.
    """

    @pytest.fixture
    async def member_with_viewer_role(self, database_url, project_data):
        """Seed a user with a viewer role assignment; return (user_id, role_id)."""
        uid = "660e8400-e29b-41d4-a716-000000000020"
        conn = await asyncpg.connect(database_url)
        try:
            await conn.execute(
                """
                INSERT INTO users (id, username, email, hashed_password,
                                   password_change_required, is_active)
                VALUES ($1, 'revokeuser', 'revokeuser@example.com',
                        $2, false, true)
                ON CONFLICT (id) DO NOTHING
                """,
                uid,
                HASHED_PASSWORD,
            )
            await conn.execute(
                """
                INSERT INTO user_project_roles (user_id, project_id, role_id,
                                                granted_by, granted_at)
                VALUES ($1, $2, $3, $4, now())
                ON CONFLICT (user_id, project_id, role_id) DO NOTHING
                """,
                uid,
                project_data,
                VIEWER_ROLE_ID,
                SUPERADMIN_USER_ID,
            )
        finally:
            await conn.close()
        return uid, VIEWER_ROLE_ID

    @pytest.mark.asyncio
    async def test_happy_path_revoke_returns_200(
        self, service_client, superadmin_token, project_data, member_with_viewer_role
    ):
        """Happy path: existing assignment revoked → 200 (line 312-313)."""
        user_id, role_id = member_with_viewer_role
        response = await service_client.delete(
            f"/api/v1/projects/{project_data}/members/{user_id}/roles/{role_id}",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert response.status == 200
        body = await response.json()
        assert body["message"] == "Role revoked"

    @pytest.mark.asyncio
    async def test_returns_404_when_assignment_not_found(
        self, service_client, superadmin_token, project_data, database_url
    ):
        """Revoking a non-existent assignment → 404 (line 315)."""
        # Use a user that has NO viewer role in this project
        uid = "660e8400-e29b-41d4-a716-000000000030"
        conn = await asyncpg.connect(database_url)
        try:
            await conn.execute(
                """
                INSERT INTO users (id, username, email, hashed_password,
                                   password_change_required, is_active)
                VALUES ($1, 'notassigneduser', 'notassigned@example.com',
                        $2, false, true)
                ON CONFLICT (id) DO NOTHING
                """,
                uid,
                HASHED_PASSWORD,
            )
            # Ensure the assignment does NOT exist
            await conn.execute(
                """
                DELETE FROM user_project_roles
                WHERE user_id = $1 AND project_id = $2 AND role_id = $3
                """,
                uid,
                project_data,
                VIEWER_ROLE_ID,
            )
        finally:
            await conn.close()

        response = await service_client.delete(
            f"/api/v1/projects/{project_data}/members/{uid}/roles/{VIEWER_ROLE_ID}",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert response.status == 404
        body = await response.json()
        assert "not found" in body["error"].lower()

    @pytest.mark.asyncio
    async def test_returns_401_without_token(
        self, service_client, project_data, member_with_viewer_role
    ):
        """No token → 401 (line 317)."""
        user_id, role_id = member_with_viewer_role
        response = await service_client.delete(
            f"/api/v1/projects/{project_data}/members/{user_id}/roles/{role_id}",
        )
        assert response.status == 401

    @pytest.mark.asyncio
    async def test_returns_403_without_change_role_permission(
        self, service_client, viewer_user_token, project_data, member_with_viewer_role
    ):
        """Viewer lacks project.members.change_role → 403 (line 321-322)."""
        user_id, role_id = member_with_viewer_role
        response = await service_client.delete(
            f"/api/v1/projects/{project_data}/members/{user_id}/roles/{role_id}",
            headers={"Authorization": f"Bearer {viewer_user_token}"},
        )
        assert response.status == 403

    @pytest.mark.asyncio
    async def test_returns_409_revoking_last_owner(
        self, service_client, superadmin_token, project_data
    ):
        """Revoking the sole owner role → 409 ConflictError (line 321-322, status_code)."""
        # superadmin is the sole owner of project_data (set in conftest.project_data trigger)
        response = await service_client.delete(
            f"/api/v1/projects/{project_data}/members/{SUPERADMIN_USER_ID}/roles/{OWNER_ROLE_ID}",
            headers={"Authorization": f"Bearer {superadmin_token}"},
        )
        assert response.status == 409
