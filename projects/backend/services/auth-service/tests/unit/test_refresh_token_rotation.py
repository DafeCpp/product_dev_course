"""Unit tests: refresh token rotation and reuse detection."""
from __future__ import annotations

from datetime import datetime, timezone
from unittest.mock import AsyncMock, patch
from uuid import uuid4, UUID

import pytest

from auth_service.core.exceptions import InvalidCredentialsError
from auth_service.domain.dto import AuthTokensResponse
from auth_service.domain.models import User
from auth_service.services.auth import AuthService


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_user() -> User:
    return User(
        id=uuid4(),
        username="tester",
        email="tester@example.com",
        hashed_password="hashed",
        password_change_required=False,
        is_active=True,
        created_at=datetime.now(timezone.utc),
        updated_at=datetime.now(timezone.utc),
    )


def _mock_perm_service() -> AsyncMock:
    svc = AsyncMock()
    svc.is_superadmin = AsyncMock(return_value=False)
    svc.get_effective_permissions = AsyncMock(return_value=AsyncMock(system_permissions=[]))
    return svc


def _build_service(
    user_repo: AsyncMock,
    revoked_repo: AsyncMock,
    family_repo: AsyncMock,
    perm_svc: AsyncMock | None = None,
) -> AuthService:
    return AuthService(
        user_repository=user_repo,
        revoked_repo=revoked_repo,
        reset_repo=AsyncMock(),
        permission_service=perm_svc or _mock_perm_service(),
        family_repo=family_repo,
    )


# ---------------------------------------------------------------------------
# test_refresh_rotates_token
# ---------------------------------------------------------------------------

class TestRefreshRotation:
    """New refresh token must differ from the old one."""

    @pytest.mark.asyncio
    async def test_refresh_rotates_token(self) -> None:
        user = _make_user()
        family_id = uuid4()
        old_jti = str(uuid4())
        exp = int(datetime.now(timezone.utc).timestamp()) + 3600

        user_repo = AsyncMock()
        user_repo.get_by_id = AsyncMock(return_value=user)

        revoked_repo = AsyncMock()
        revoked_repo.is_revoked = AsyncMock(return_value=False)
        revoked_repo.revoke = AsyncMock()

        family_repo = AsyncMock()
        family_repo.is_revoked = AsyncMock(return_value=False)
        family_repo.create = AsyncMock(return_value=uuid4())

        svc = _build_service(user_repo, revoked_repo, family_repo)

        with patch("auth_service.services.auth.decode_token") as mock_decode:
            mock_decode.return_value = {
                "type": "refresh",
                "jti": old_jti,
                "sub": str(user.id),
                "exp": exp,
                "fid": str(family_id),
            }
            tokens = await svc.refresh_token("old_refresh_token")

        assert isinstance(tokens, AuthTokensResponse)
        assert tokens.refresh_token != "old_refresh_token"
        assert tokens.refresh_token != ""

    @pytest.mark.asyncio
    async def test_refresh_revokes_old_jti(self) -> None:
        """After refresh, the old JTI must be added to revoked_tokens."""
        user = _make_user()
        family_id = uuid4()
        old_jti = str(uuid4())
        exp = int(datetime.now(timezone.utc).timestamp()) + 3600

        user_repo = AsyncMock()
        user_repo.get_by_id = AsyncMock(return_value=user)

        revoked_repo = AsyncMock()
        revoked_repo.is_revoked = AsyncMock(return_value=False)
        revoked_repo.revoke = AsyncMock()

        family_repo = AsyncMock()
        family_repo.is_revoked = AsyncMock(return_value=False)
        family_repo.create = AsyncMock(return_value=uuid4())

        svc = _build_service(user_repo, revoked_repo, family_repo)

        with patch("auth_service.services.auth.decode_token") as mock_decode:
            mock_decode.return_value = {
                "type": "refresh",
                "jti": old_jti,
                "sub": str(user.id),
                "exp": exp,
                "fid": str(family_id),
            }
            await svc.refresh_token("old_refresh_token")

        revoked_repo.revoke.assert_called_once()
        call_args = revoked_repo.revoke.call_args
        assert call_args.args[0] == UUID(old_jti)


# ---------------------------------------------------------------------------
# test_reuse_detection
# ---------------------------------------------------------------------------

class TestReuseDetection:
    """Reusing a consumed refresh token must be rejected with 401."""

    @pytest.mark.asyncio
    async def test_reuse_detection_returns_401(self) -> None:
        user = _make_user()
        family_id = uuid4()
        old_jti = str(uuid4())
        exp = int(datetime.now(timezone.utc).timestamp()) + 3600

        user_repo = AsyncMock()
        revoked_repo = AsyncMock()
        # Simulate: jti already in revoked list (reuse)
        revoked_repo.is_revoked = AsyncMock(return_value=True)

        family_repo = AsyncMock()
        family_repo.is_revoked = AsyncMock(return_value=False)
        family_repo.revoke_family = AsyncMock()

        svc = _build_service(user_repo, revoked_repo, family_repo)

        with patch("auth_service.services.auth.decode_token") as mock_decode:
            mock_decode.return_value = {
                "type": "refresh",
                "jti": old_jti,
                "sub": str(user.id),
                "exp": exp,
                "fid": str(family_id),
            }
            with pytest.raises(InvalidCredentialsError):
                await svc.refresh_token("reused_refresh_token")

    @pytest.mark.asyncio
    async def test_reuse_revokes_family(self) -> None:
        """On reuse detection the whole family must be revoked."""
        user = _make_user()
        family_id = uuid4()
        old_jti = str(uuid4())
        exp = int(datetime.now(timezone.utc).timestamp()) + 3600

        user_repo = AsyncMock()
        revoked_repo = AsyncMock()
        revoked_repo.is_revoked = AsyncMock(return_value=True)

        family_repo = AsyncMock()
        family_repo.is_revoked = AsyncMock(return_value=False)
        family_repo.revoke_family = AsyncMock()

        svc = _build_service(user_repo, revoked_repo, family_repo)

        with patch("auth_service.services.auth.decode_token") as mock_decode:
            mock_decode.return_value = {
                "type": "refresh",
                "jti": old_jti,
                "sub": str(user.id),
                "exp": exp,
                "fid": str(family_id),
            }
            with pytest.raises(InvalidCredentialsError):
                await svc.refresh_token("reused_refresh_token")

        family_repo.revoke_family.assert_called_once_with(family_id)

    @pytest.mark.asyncio
    async def test_revoked_family_blocks_refresh(self) -> None:
        """A token whose family is already revoked must be rejected."""
        user = _make_user()
        family_id = uuid4()
        jti = str(uuid4())
        exp = int(datetime.now(timezone.utc).timestamp()) + 3600

        user_repo = AsyncMock()
        revoked_repo = AsyncMock()
        revoked_repo.is_revoked = AsyncMock(return_value=False)

        family_repo = AsyncMock()
        # Family is revoked (e.g. because of previous reuse or logout)
        family_repo.is_revoked = AsyncMock(return_value=True)

        svc = _build_service(user_repo, revoked_repo, family_repo)

        with patch("auth_service.services.auth.decode_token") as mock_decode:
            mock_decode.return_value = {
                "type": "refresh",
                "jti": jti,
                "sub": str(user.id),
                "exp": exp,
                "fid": str(family_id),
            }
            with pytest.raises(InvalidCredentialsError, match="family"):
                await svc.refresh_token("stolen_refresh_token")


# ---------------------------------------------------------------------------
# test_logout_revokes_family
# ---------------------------------------------------------------------------

class TestLogoutRevokesFamily:
    """Logout must revoke the token family when fid claim is present."""

    @pytest.mark.asyncio
    async def test_logout_revokes_family(self) -> None:
        family_id = uuid4()
        jti = str(uuid4())
        user_id = uuid4()
        exp = int(datetime.now(timezone.utc).timestamp()) + 3600

        revoked_repo = AsyncMock()
        revoked_repo.revoke = AsyncMock()

        family_repo = AsyncMock()
        family_repo.revoke_family = AsyncMock()

        svc = _build_service(AsyncMock(), revoked_repo, family_repo)

        with patch("auth_service.services.auth.decode_token") as mock_decode:
            mock_decode.return_value = {
                "type": "refresh",
                "jti": jti,
                "sub": str(user_id),
                "exp": exp,
                "fid": str(family_id),
            }
            await svc.logout("refresh_token")

        revoked_repo.revoke.assert_called_once()
        family_repo.revoke_family.assert_called_once_with(family_id)

    @pytest.mark.asyncio
    async def test_logout_without_fid_does_not_call_family_repo(self) -> None:
        """Backward-compat: tokens without fid should still work."""
        jti = str(uuid4())
        user_id = uuid4()
        exp = int(datetime.now(timezone.utc).timestamp()) + 3600

        revoked_repo = AsyncMock()
        revoked_repo.revoke = AsyncMock()

        family_repo = AsyncMock()
        family_repo.revoke_family = AsyncMock()

        svc = _build_service(AsyncMock(), revoked_repo, family_repo)

        with patch("auth_service.services.auth.decode_token") as mock_decode:
            mock_decode.return_value = {
                "type": "refresh",
                "jti": jti,
                "sub": str(user_id),
                "exp": exp,
                # no "fid" claim — old token format
            }
            await svc.logout("old_refresh_token")

        revoked_repo.revoke.assert_called_once()
        family_repo.revoke_family.assert_not_called()


# ---------------------------------------------------------------------------
# Backward compatibility: tokens without fid
# ---------------------------------------------------------------------------

class TestBackwardCompatibility:
    """Old refresh tokens without fid must still work."""

    @pytest.mark.asyncio
    async def test_refresh_without_fid_succeeds(self) -> None:
        user = _make_user()
        jti = str(uuid4())
        exp = int(datetime.now(timezone.utc).timestamp()) + 3600

        user_repo = AsyncMock()
        user_repo.get_by_id = AsyncMock(return_value=user)

        revoked_repo = AsyncMock()
        revoked_repo.is_revoked = AsyncMock(return_value=False)
        revoked_repo.revoke = AsyncMock()

        family_repo = AsyncMock()
        # is_revoked should NOT be called when there is no fid
        family_repo.is_revoked = AsyncMock(return_value=False)
        family_repo.create = AsyncMock(return_value=uuid4())

        svc = _build_service(user_repo, revoked_repo, family_repo)

        with patch("auth_service.services.auth.decode_token") as mock_decode:
            mock_decode.return_value = {
                "type": "refresh",
                "jti": jti,
                "sub": str(user.id),
                "exp": exp,
                # no fid
            }
            tokens = await svc.refresh_token("legacy_refresh_token")

        assert isinstance(tokens, AuthTokensResponse)
        # family-level is_revoked must not have been called (no fid)
        family_repo.is_revoked.assert_not_called()
