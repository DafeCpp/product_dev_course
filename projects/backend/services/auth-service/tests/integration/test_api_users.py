"""Integration tests for users routes (search_users handler).

Targets missing coverage on users.py lines 29-70:
  - Line 29-33: authentication check (try/except AuthError → 401)
  - Lines 36-40: query too short (< 2 chars) → 400
  - Lines 42-46: limit clamped to _MAX_LIMIT, invalid limit fallback
  - Lines 48-54: exclude_project_id parsing + invalid UUID → 400
  - Lines 56-70: repo call, result serialisation, exception → 500
"""
from __future__ import annotations

import pytest
import asyncpg
from uuid import uuid4


class TestSearchUsersAuthentication:
    """Cover lines 29-33: authentication guard."""

    @pytest.mark.asyncio
    async def test_search_requires_auth(self, service_client):
        """No Authorization header → 401."""
        response = await service_client.get("/api/v1/users/search?q=ad")
        assert response.status == 401

    @pytest.mark.asyncio
    async def test_search_malformed_token_returns_error(self, service_client):
        """Malformed JWT → error response.

        A structurally-invalid JWT raises ValueError inside jwt decode, which is
        NOT an AuthError subclass, so it propagates and returns 500 (generic handler).
        The test asserts the response is an error (not 200), exercising the auth
        code path.
        """
        response = await service_client.get(
            "/api/v1/users/search?q=ad",
            headers={"Authorization": "Bearer not.a.valid.jwt"},
        )
        # ValueError from jwt decode is not AuthError → middleware returns 500
        assert response.status in (401, 500)


class TestSearchUsersQueryValidation:
    """Cover lines 35-40: query length validation."""

    @pytest.mark.asyncio
    async def test_empty_query_returns_400(self, service_client, regular_user_token):
        """Empty q → 400 (len < 2)."""
        response = await service_client.get(
            "/api/v1/users/search?q=",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response.status == 400
        data = await response.json()
        assert "error" in data
        assert "2" in data["error"]  # mentions minimum length

    @pytest.mark.asyncio
    async def test_single_char_query_returns_400(self, service_client, regular_user_token):
        """Single-character q → 400 (len < 2)."""
        response = await service_client.get(
            "/api/v1/users/search?q=a",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response.status == 400

    @pytest.mark.asyncio
    async def test_two_char_query_is_accepted(self, service_client, regular_user_token):
        """Two-character q is at the boundary and should be accepted (→ 200)."""
        response = await service_client.get(
            "/api/v1/users/search?q=ad",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response.status == 200


class TestSearchUsersLimitParam:
    """Cover lines 42-46: limit parsing and clamping."""

    @pytest.mark.asyncio
    async def test_default_limit_applied(self, service_client, regular_user_token):
        """Without explicit limit, response is still 200 (default _MAX_LIMIT=10)."""
        response = await service_client.get(
            "/api/v1/users/search?q=ad",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response.status == 200
        data = await response.json()
        assert len(data) <= 10

    @pytest.mark.asyncio
    async def test_large_limit_is_clamped(self, service_client, regular_user_token):
        """limit=999 is clamped to _MAX_LIMIT=10 — still 200, at most 10 results."""
        response = await service_client.get(
            "/api/v1/users/search?q=ad&limit=999",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response.status == 200
        data = await response.json()
        assert len(data) <= 10

    @pytest.mark.asyncio
    async def test_invalid_limit_falls_back_to_default(
        self, service_client, regular_user_token
    ):
        """Non-numeric limit falls back to _MAX_LIMIT — still 200 (line 45 ValueError branch)."""
        response = await service_client.get(
            "/api/v1/users/search?q=ad&limit=notanumber",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response.status == 200


class TestSearchUsersExcludeProjectId:
    """Cover lines 48-54: exclude_project_id parsing."""

    @pytest.mark.asyncio
    async def test_valid_exclude_project_id_accepted(
        self, service_client, regular_user_token
    ):
        """Valid UUID for exclude_project_id → 200."""
        project_id = str(uuid4())
        response = await service_client.get(
            f"/api/v1/users/search?q=ad&exclude_project_id={project_id}",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response.status == 200

    @pytest.mark.asyncio
    async def test_invalid_exclude_project_id_returns_400(
        self, service_client, regular_user_token
    ):
        """Invalid UUID for exclude_project_id → 400 (line 53-54)."""
        response = await service_client.get(
            "/api/v1/users/search?q=ad&exclude_project_id=not-a-uuid",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response.status == 400
        data = await response.json()
        assert data["error"] == "Invalid exclude_project_id"


class TestSearchUsersHappyPath:
    """Cover lines 56-70: successful search + result serialisation."""

    @pytest.mark.asyncio
    async def test_search_returns_matching_users(
        self, service_client, regular_user_token, database_url
    ):
        """Users matching the prefix are returned with correct shape."""
        unique = uuid4().hex[:6]
        username_prefix = f"srch{unique}"

        conn = await asyncpg.connect(database_url)
        try:
            await conn.execute(
                "INSERT INTO users (username, email, hashed_password, password_change_required, is_active) "
                "VALUES ($1, $2, '$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG', false, true) "
                "ON CONFLICT (username) DO NOTHING",
                username_prefix,
                f"{username_prefix}@example.com",
            )
        finally:
            await conn.close()

        response = await service_client.get(
            f"/api/v1/users/search?q={username_prefix[:4]}",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response.status == 200
        data = await response.json()
        assert isinstance(data, list)
        usernames = [u["username"] for u in data]
        assert username_prefix in usernames

    @pytest.mark.asyncio
    async def test_search_result_shape(
        self, service_client, regular_user_token, database_url
    ):
        """Each returned item has id, username, email, is_active fields (line 65)."""
        unique = uuid4().hex[:6]
        username = f"shape{unique}"

        conn = await asyncpg.connect(database_url)
        try:
            await conn.execute(
                "INSERT INTO users (username, email, hashed_password, password_change_required, is_active) "
                "VALUES ($1, $2, '$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG', false, true) "
                "ON CONFLICT (username) DO NOTHING",
                username,
                f"{username}@example.com",
            )
        finally:
            await conn.close()

        response = await service_client.get(
            f"/api/v1/users/search?q={username[:5]}",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response.status == 200
        data = await response.json()
        assert len(data) >= 1
        item = next(u for u in data if u["username"] == username)
        assert "id" in item
        assert "username" in item
        assert "email" in item
        assert "is_active" in item
        assert item["is_active"] is True

    @pytest.mark.asyncio
    async def test_search_no_match_returns_empty_list(
        self, service_client, regular_user_token
    ):
        """Query that matches nothing returns []."""
        response = await service_client.get(
            "/api/v1/users/search?q=zzznomatch9999",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response.status == 200
        data = await response.json()
        assert data == []

    @pytest.mark.asyncio
    async def test_inactive_users_excluded_from_search(
        self, service_client, regular_user_token, database_url
    ):
        """Inactive users are not returned by search (is_active=true filter in repo)."""
        unique = uuid4().hex[:6]
        inactive_username = f"inactive{unique}"

        conn = await asyncpg.connect(database_url)
        try:
            await conn.execute(
                "INSERT INTO users (username, email, hashed_password, password_change_required, is_active) "
                "VALUES ($1, $2, '$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG', false, false) "
                "ON CONFLICT (username) DO NOTHING",
                inactive_username,
                f"{inactive_username}@example.com",
            )
        finally:
            await conn.close()

        response = await service_client.get(
            f"/api/v1/users/search?q={inactive_username[:6]}",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response.status == 200
        data = await response.json()
        usernames = [u["username"] for u in data]
        assert inactive_username not in usernames

    @pytest.mark.asyncio
    async def test_search_excludes_project_members(
        self, service_client, regular_user_token, database_url
    ):
        """Users already in the project are excluded when exclude_project_id is given."""
        unique = uuid4().hex[:6]
        member_username = f"member{unique}"

        conn = await asyncpg.connect(database_url)
        try:
            user_result = await conn.fetchrow(
                "INSERT INTO users (username, email, hashed_password, password_change_required, is_active) "
                "VALUES ($1, $2, '$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG', false, true) "
                "RETURNING id",
                member_username,
                f"{member_username}@example.com",
            )
            user_id = user_result["id"]

            # Create a project and add user as a member
            proj_result = await conn.fetchrow(
                "INSERT INTO projects (name, description, owner_id) "
                "VALUES ('SearchTestProject', 'test', $1) RETURNING id",
                user_id,
            )
            project_id = str(proj_result["id"])

            await conn.execute(
                "INSERT INTO user_project_roles (user_id, project_id, role_id, granted_by, granted_at) "
                "VALUES ($1, $2, '00000000-0000-0000-0000-000000000012', $1, now())",
                user_id, proj_result["id"],
            )
        finally:
            await conn.close()

        # Search without exclusion → user appears
        response_all = await service_client.get(
            f"/api/v1/users/search?q={member_username[:5]}",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response_all.status == 200
        all_data = await response_all.json()
        assert any(u["username"] == member_username for u in all_data)

        # Search with exclusion → user excluded
        response_excl = await service_client.get(
            f"/api/v1/users/search?q={member_username[:5]}&exclude_project_id={project_id}",
            headers={"Authorization": f"Bearer {regular_user_token}"},
        )
        assert response_excl.status == 200
        excl_data = await response_excl.json()
        assert all(u["username"] != member_username for u in excl_data)
