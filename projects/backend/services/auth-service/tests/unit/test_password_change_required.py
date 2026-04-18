"""Tests for password_change_required enforcement."""
from __future__ import annotations

from datetime import datetime, timezone
from unittest.mock import AsyncMock, MagicMock, patch
from uuid import uuid4

import pytest

from auth_service.domain.dto import AuthTokensResponse
from auth_service.domain.models import User
from auth_service.services.auth import AuthService
from auth_service.services.jwt import (
    create_access_token,
    decode_token,
    get_claims_from_token,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_user(*, password_change_required: bool = False, is_active: bool = True) -> User:
    return User(
        id=uuid4(),
        username="testuser",
        email="test@example.com",
        hashed_password="hashed",
        password_change_required=password_change_required,
        is_active=is_active,
        created_at=datetime.now(timezone.utc),
        updated_at=datetime.now(timezone.utc),
    )


@pytest.fixture()
def mock_permission_service() -> AsyncMock:
    perm_svc = AsyncMock()
    perm_svc.is_superadmin = AsyncMock(return_value=False)
    perm_svc.get_effective_permissions = AsyncMock(
        return_value=MagicMock(system_permissions=[])
    )
    perm_svc.list_system_role_names = AsyncMock(return_value=[])
    return perm_svc


@pytest.fixture()
def auth_service(mock_permission_service: AsyncMock) -> AuthService:
    user_repo = AsyncMock()
    revoked_repo = AsyncMock()
    reset_repo = AsyncMock()
    invite_repo = AsyncMock()
    return AuthService(
        user_repository=user_repo,
        revoked_repo=revoked_repo,
        reset_repo=reset_repo,
        permission_service=mock_permission_service,
        invite_repo=invite_repo,
        registration_mode="open",
    )


# ---------------------------------------------------------------------------
# 1. login returns password_change_required flag
# ---------------------------------------------------------------------------

class TestLoginPasswordChangeRequired:
    @pytest.mark.asyncio
    async def test_login_returns_password_change_required_flag(
        self,
        auth_service: AuthService,
        mock_permission_service: AsyncMock,
    ) -> None:
        """login() sets password_change_required=True on tokens when the flag is set."""
        user = _make_user(password_change_required=True)
        auth_service._user_repo.get_by_username = AsyncMock(return_value=user)

        with patch("auth_service.services.auth.verify_password", return_value=True):
            _, tokens = await auth_service.login("testuser", "password")

        assert tokens.password_change_required is True

    @pytest.mark.asyncio
    async def test_login_no_flag_when_not_required(
        self,
        auth_service: AuthService,
    ) -> None:
        """login() leaves password_change_required=False when user has no flag set."""
        user = _make_user(password_change_required=False)
        auth_service._user_repo.get_by_username = AsyncMock(return_value=user)

        with patch("auth_service.services.auth.verify_password", return_value=True):
            _, tokens = await auth_service.login("testuser", "password")

        assert tokens.password_change_required is False


# ---------------------------------------------------------------------------
# 2. JWT contains pcr claim when required
# ---------------------------------------------------------------------------

class TestJwtPcrClaim:
    def test_jwt_contains_pcr_claim_when_required(self) -> None:
        """Access token contains pcr=True when password_change_required is set."""
        token = create_access_token("user-123", password_change_required=True)
        claims = get_claims_from_token(token)
        assert claims["pcr"] is True

    def test_jwt_omits_pcr_claim_when_not_required(self) -> None:
        """Access token does NOT contain pcr when password_change_required is False."""
        token = create_access_token("user-123", password_change_required=False)
        payload = decode_token(token)
        assert "pcr" not in payload

    @pytest.mark.asyncio
    async def test_login_access_token_has_pcr_claim(
        self,
        auth_service: AuthService,
    ) -> None:
        """Access token issued at login contains pcr=True when flag is set."""
        user = _make_user(password_change_required=True)
        auth_service._user_repo.get_by_username = AsyncMock(return_value=user)

        with patch("auth_service.services.auth.verify_password", return_value=True):
            _, tokens = await auth_service.login("testuser", "password")

        payload = decode_token(tokens.access_token)
        assert payload.get("pcr") is True


# ---------------------------------------------------------------------------
# 3. change_password clears pcr flag
# ---------------------------------------------------------------------------

class TestChangePasswordClearsPcr:
    @pytest.mark.asyncio
    async def test_change_password_clears_pcr_flag(
        self,
        auth_service: AuthService,
    ) -> None:
        """After change_password the new tokens must NOT contain pcr."""
        user = _make_user(password_change_required=True)
        updated_user = _make_user(password_change_required=False)

        auth_service._user_repo.get_by_id = AsyncMock(return_value=user)
        auth_service._user_repo.update_password = AsyncMock(return_value=updated_user)

        with patch("auth_service.services.auth.verify_password", return_value=True), \
             patch("auth_service.services.auth.hash_password", return_value="newhash"):
            _, new_tokens = await auth_service.change_password(
                user.id, "oldpass", "newpass"
            )

        assert new_tokens.password_change_required is False
        payload = decode_token(new_tokens.access_token)
        assert "pcr" not in payload

    @pytest.mark.asyncio
    async def test_change_password_calls_update_with_false_flag(
        self,
        auth_service: AuthService,
    ) -> None:
        """update_password is called with password_change_required=False."""
        user = _make_user(password_change_required=True)
        updated_user = _make_user(password_change_required=False)

        auth_service._user_repo.get_by_id = AsyncMock(return_value=user)
        auth_service._user_repo.update_password = AsyncMock(return_value=updated_user)

        with patch("auth_service.services.auth.verify_password", return_value=True), \
             patch("auth_service.services.auth.hash_password", return_value="newhash"):
            await auth_service.change_password(user.id, "oldpass", "newpass")

        auth_service._user_repo.update_password.assert_called_once()
        _, kwargs = auth_service._user_repo.update_password.call_args
        assert kwargs.get("password_change_required") is False


# ---------------------------------------------------------------------------
# 4. change_password returns new tokens
# ---------------------------------------------------------------------------

class TestChangePasswordReturnsNewTokens:
    @pytest.mark.asyncio
    async def test_change_password_returns_new_tokens(
        self,
        auth_service: AuthService,
    ) -> None:
        """change_password returns a tuple (User, AuthTokensResponse)."""
        user = _make_user(password_change_required=True)
        updated_user = _make_user(password_change_required=False)

        auth_service._user_repo.get_by_id = AsyncMock(return_value=user)
        auth_service._user_repo.update_password = AsyncMock(return_value=updated_user)

        with patch("auth_service.services.auth.verify_password", return_value=True), \
             patch("auth_service.services.auth.hash_password", return_value="newhash"):
            result = await auth_service.change_password(user.id, "oldpass", "newpass")

        result_user, new_tokens = result
        assert result_user is updated_user
        assert isinstance(new_tokens, AuthTokensResponse)
        assert new_tokens.access_token
        assert new_tokens.refresh_token

    @pytest.mark.asyncio
    async def test_change_password_new_tokens_are_valid_jwt(
        self,
        auth_service: AuthService,
    ) -> None:
        """Tokens returned by change_password are decodable JWTs."""
        user = _make_user(password_change_required=True)
        updated_user = _make_user(password_change_required=False)

        auth_service._user_repo.get_by_id = AsyncMock(return_value=user)
        auth_service._user_repo.update_password = AsyncMock(return_value=updated_user)

        with patch("auth_service.services.auth.verify_password", return_value=True), \
             patch("auth_service.services.auth.hash_password", return_value="newhash"):
            _, new_tokens = await auth_service.change_password(user.id, "oldpass", "newpass")

        access_payload = decode_token(new_tokens.access_token)
        assert access_payload["sub"] == str(user.id)
        assert access_payload.get("type") == "access"
