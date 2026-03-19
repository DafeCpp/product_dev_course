"""Unit tests for user search endpoint and repository method."""
from __future__ import annotations

from unittest.mock import AsyncMock, MagicMock, patch
from uuid import UUID, uuid4

import pytest
from aiohttp import web
from aiohttp.test_utils import TestClient, TestServer

from auth_service.api.routes.users import setup_routes
from auth_service.repositories.users import UserRepository


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_user_row(
    username: str = "alice",
    email: str = "alice@example.com",
    is_active: bool = True,
    user_id: UUID | None = None,
) -> dict[str, object]:
    return {
        "id": str(user_id or uuid4()),
        "username": username,
        "email": email,
        "is_active": is_active,
    }


def _mock_pool() -> MagicMock:
    mock_pool = AsyncMock()
    mock_conn = AsyncMock()
    mock_cm = MagicMock()
    mock_cm.__aenter__ = AsyncMock(return_value=mock_conn)
    mock_cm.__aexit__ = AsyncMock(return_value=None)
    mock_pool.acquire = MagicMock(return_value=mock_cm)
    return mock_pool, mock_conn


# ---------------------------------------------------------------------------
# Repository unit tests
# ---------------------------------------------------------------------------

class TestUserRepositorySearchByUsername:
    """Tests for UserRepository.search_by_username."""

    @pytest.mark.asyncio
    async def test_search_returns_matching_users(self):
        """Prefix-matched active users are returned."""
        pool, conn = _mock_pool()
        rows = [
            _make_user_row("alice"),
            _make_user_row("alicia"),
        ]
        conn.fetch = AsyncMock(return_value=[
            {
                "id": UUID(r["id"]),  # type: ignore[arg-type]
                "username": r["username"],
                "email": r["email"],
                "is_active": r["is_active"],
            }
            for r in rows
        ])

        repo = UserRepository(pool)
        result = await repo.search_by_username("ali")

        assert len(result) == 2
        assert result[0]["username"] == "alice"
        assert result[1]["username"] == "alicia"
        conn.fetch.assert_called_once()
        call_args = conn.fetch.call_args[0]
        assert "ILIKE $1 || '%'" in call_args[0]
        assert call_args[1] == "ali"

    @pytest.mark.asyncio
    async def test_search_excludes_project_members(self):
        """When exclude_project_id is given, subquery filters are passed to DB."""
        pool, conn = _mock_pool()
        project_id = uuid4()
        conn.fetch = AsyncMock(return_value=[])

        repo = UserRepository(pool)
        await repo.search_by_username("ali", exclude_project_id=project_id)

        call_args = conn.fetch.call_args[0]
        # Second positional param must be the project_id UUID (not None)
        assert call_args[2] == project_id

    @pytest.mark.asyncio
    async def test_search_passes_none_for_no_exclude(self):
        """When exclude_project_id is None, None is forwarded to DB."""
        pool, conn = _mock_pool()
        conn.fetch = AsyncMock(return_value=[])

        repo = UserRepository(pool)
        await repo.search_by_username("bob")

        call_args = conn.fetch.call_args[0]
        assert call_args[2] is None

    @pytest.mark.asyncio
    async def test_search_limits_results(self):
        """Limit argument is forwarded to the query."""
        pool, conn = _mock_pool()
        conn.fetch = AsyncMock(return_value=[])

        repo = UserRepository(pool)
        await repo.search_by_username("x", limit=5)

        call_args = conn.fetch.call_args[0]
        assert call_args[3] == 5

    @pytest.mark.asyncio
    async def test_search_only_active_users(self):
        """SQL contains is_active = true filter."""
        pool, conn = _mock_pool()
        conn.fetch = AsyncMock(return_value=[])

        repo = UserRepository(pool)
        await repo.search_by_username("z")

        sql: str = conn.fetch.call_args[0][0]
        assert "is_active = true" in sql


# ---------------------------------------------------------------------------
# HTTP handler tests (mock repository via patch)
# ---------------------------------------------------------------------------

VALID_TOKEN = "valid.jwt.token"
REQUESTER_ID = uuid4()
REQUESTER_UUID = UUID(str(REQUESTER_ID))


def _make_app() -> web.Application:
    app = web.Application()
    setup_routes(app)
    return app


def _patch_auth(active: bool = True):
    """Patch JWT decode and DB lookup used in _require_auth."""
    user_mock = MagicMock()
    user_mock.id = REQUESTER_UUID
    user_mock.is_active = active

    return (
        patch(
            "auth_service.api.routes.users.jwt_get_user_id",
            return_value=str(REQUESTER_UUID),
        ),
        patch(
            "auth_service.api.routes.users.UserRepository.get_by_id",
            new=AsyncMock(return_value=user_mock),
        ),
        patch(
            "auth_service.api.routes.users.get_pool",
            new=AsyncMock(return_value=AsyncMock()),
        ),
    )


@pytest.fixture
async def client(aiohttp_client):  # type: ignore[no-untyped-def]
    app = _make_app()
    return await aiohttp_client(app)


class TestSearchUsersEndpoint:
    """Integration-style tests for GET /api/v1/users/search."""

    @pytest.mark.asyncio
    async def test_search_requires_min_query_length(self, aiohttp_client):
        """q shorter than 2 chars returns 400."""
        with (
            patch("auth_service.api.routes.users.jwt_get_user_id", return_value=str(REQUESTER_UUID)),
            patch("auth_service.api.routes.users.get_pool", new=AsyncMock(return_value=AsyncMock())),
        ):
            user_mock = MagicMock()
            user_mock.id = REQUESTER_UUID
            user_mock.is_active = True
            with patch("auth_service.api.routes.users.UserRepository") as MockRepo:
                instance = MockRepo.return_value
                instance.get_by_id = AsyncMock(return_value=user_mock)
                instance.search_by_username = AsyncMock(return_value=[])

                client = await aiohttp_client(_make_app())
                resp = await client.get(
                    "/api/v1/users/search?q=a",
                    headers={"Authorization": f"Bearer {VALID_TOKEN}"},
                )
                assert resp.status == 400
                body = await resp.json()
                assert "2" in body["error"]

    @pytest.mark.asyncio
    async def test_search_returns_matching_users(self, aiohttp_client):
        """Valid query returns matched users as JSON array."""
        user_mock = MagicMock()
        user_mock.id = REQUESTER_UUID
        user_mock.is_active = True

        search_results = [
            {"id": str(uuid4()), "username": "alice", "email": "alice@x.com", "is_active": True},
            {"id": str(uuid4()), "username": "alicia", "email": "alicia@x.com", "is_active": True},
        ]

        with (
            patch("auth_service.api.routes.users.jwt_get_user_id", return_value=str(REQUESTER_UUID)),
            patch("auth_service.api.routes.users.get_pool", new=AsyncMock(return_value=AsyncMock())),
            patch("auth_service.api.routes.users.UserRepository") as MockRepo,
        ):
            instance = MockRepo.return_value
            instance.get_by_id = AsyncMock(return_value=user_mock)
            instance.search_by_username = AsyncMock(return_value=search_results)

            client = await aiohttp_client(_make_app())
            resp = await client.get(
                "/api/v1/users/search?q=ali",
                headers={"Authorization": f"Bearer {VALID_TOKEN}"},
            )
            assert resp.status == 200
            body = await resp.json()
            assert len(body) == 2
            assert body[0]["username"] == "alice"

    @pytest.mark.asyncio
    async def test_search_excludes_project_members(self, aiohttp_client):
        """exclude_project_id is parsed and forwarded to the repository."""
        user_mock = MagicMock()
        user_mock.id = REQUESTER_UUID
        user_mock.is_active = True
        project_id = uuid4()

        with (
            patch("auth_service.api.routes.users.jwt_get_user_id", return_value=str(REQUESTER_UUID)),
            patch("auth_service.api.routes.users.get_pool", new=AsyncMock(return_value=AsyncMock())),
            patch("auth_service.api.routes.users.UserRepository") as MockRepo,
        ):
            instance = MockRepo.return_value
            instance.get_by_id = AsyncMock(return_value=user_mock)
            instance.search_by_username = AsyncMock(return_value=[])

            client = await aiohttp_client(_make_app())
            resp = await client.get(
                f"/api/v1/users/search?q=bob&exclude_project_id={project_id}",
                headers={"Authorization": f"Bearer {VALID_TOKEN}"},
            )
            assert resp.status == 200
            instance.search_by_username.assert_called_once_with(
                query="bob",
                limit=10,
                exclude_project_id=project_id,
            )

    @pytest.mark.asyncio
    async def test_search_limits_results(self, aiohttp_client):
        """limit param is capped at 10."""
        user_mock = MagicMock()
        user_mock.id = REQUESTER_UUID
        user_mock.is_active = True

        with (
            patch("auth_service.api.routes.users.jwt_get_user_id", return_value=str(REQUESTER_UUID)),
            patch("auth_service.api.routes.users.get_pool", new=AsyncMock(return_value=AsyncMock())),
            patch("auth_service.api.routes.users.UserRepository") as MockRepo,
        ):
            instance = MockRepo.return_value
            instance.get_by_id = AsyncMock(return_value=user_mock)
            instance.search_by_username = AsyncMock(return_value=[])

            client = await aiohttp_client(_make_app())
            resp = await client.get(
                "/api/v1/users/search?q=bo&limit=999",
                headers={"Authorization": f"Bearer {VALID_TOKEN}"},
            )
            assert resp.status == 200
            call_kwargs = instance.search_by_username.call_args
            assert call_kwargs.kwargs["limit"] == 10

    @pytest.mark.asyncio
    async def test_search_only_active_users(self, aiohttp_client):
        """Inactive requester is rejected (UserNotFoundError -> 404)."""
        user_mock = MagicMock()
        user_mock.id = REQUESTER_UUID
        user_mock.is_active = False  # inactive user

        with (
            patch("auth_service.api.routes.users.jwt_get_user_id", return_value=str(REQUESTER_UUID)),
            patch("auth_service.api.routes.users.get_pool", new=AsyncMock(return_value=AsyncMock())),
            patch("auth_service.api.routes.users.UserRepository") as MockRepo,
        ):
            instance = MockRepo.return_value
            instance.get_by_id = AsyncMock(return_value=user_mock)

            client = await aiohttp_client(_make_app())
            resp = await client.get(
                "/api/v1/users/search?q=bo",
                headers={"Authorization": f"Bearer {VALID_TOKEN}"},
            )
            # UserNotFoundError has status_code=404; inactive users are treated
            # as not found to avoid leaking information about account state.
            assert resp.status == 404
