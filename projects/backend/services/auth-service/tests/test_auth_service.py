"""Unit tests for auth_service.services.auth module."""
from __future__ import annotations

from datetime import datetime, timedelta, timezone
from unittest.mock import AsyncMock, MagicMock, patch
from uuid import uuid4

import pytest

from auth_service.core.exceptions import (
    ConflictError,
    ForbiddenError,
    InvalidCredentialsError,
    InvalidTokenError,
    NotFoundError,
    UserAlreadyExistsError,
    UserNotFoundError,
)
from auth_service.domain.dto import AuthTokensResponse
from auth_service.domain.models import InviteToken, User
from auth_service.services.auth import AuthService


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture
def mock_repos():
    """Create mock repositories."""
    user_repo = AsyncMock()
    revoked_repo = AsyncMock()
    reset_repo = AsyncMock()
    invite_repo = AsyncMock()
    return user_repo, revoked_repo, reset_repo, invite_repo


@pytest.fixture
def auth_service_open(mock_repos):
    """Create AuthService in open registration mode."""
    user_repo, revoked_repo, reset_repo, invite_repo = mock_repos
    return AuthService(
        user_repository=user_repo,
        revoked_repo=revoked_repo,
        reset_repo=reset_repo,
        invite_repo=invite_repo,
        registration_mode="open",
    )


@pytest.fixture
def auth_service_invite(mock_repos):
    """Create AuthService in invite registration mode."""
    user_repo, revoked_repo, reset_repo, invite_repo = mock_repos
    return AuthService(
        user_repository=user_repo,
        revoked_repo=revoked_repo,
        reset_repo=reset_repo,
        invite_repo=invite_repo,
        registration_mode="invite",
    )


@pytest.fixture
def sample_user():
    """Create a sample user."""
    return User(
        id=uuid4(),
        username="testuser",
        email="test@example.com",
        hashed_password="hashed123",
        password_change_required=False,
        is_admin=False,
        is_active=True,
        created_at=datetime.now(timezone.utc),
        updated_at=datetime.now(timezone.utc),
    )


@pytest.fixture
def sample_admin():
    """Create a sample admin user."""
    return User(
        id=uuid4(),
        username="admin",
        email="admin@example.com",
        hashed_password="hashed123",
        password_change_required=False,
        is_admin=True,
        is_active=True,
        created_at=datetime.now(timezone.utc),
        updated_at=datetime.now(timezone.utc),
    )


@pytest.fixture
def sample_invite():
    """Create a sample invite token."""
    return InviteToken(
        id=uuid4(),
        token=uuid4(),
        created_by=uuid4(),
        email_hint="test@example.com",
        expires_at=datetime.now(timezone.utc) + timedelta(hours=24),
        used_at=None,
        used_by=None,
        created_at=datetime.now(timezone.utc),
    )


# ---------------------------------------------------------------------------
# Bootstrap Admin Tests
# ---------------------------------------------------------------------------

class TestBootstrapAdmin:
    """Tests for bootstrap_admin method."""

    @pytest.mark.asyncio
    async def test_bootstrap_admin_success(self, auth_service_open, mock_repos):
        """Test successful admin bootstrap."""
        user_repo, *_ = mock_repos

        # Setup mocks
        user_repo.count_admins = AsyncMock(return_value=0)
        user_repo.user_exists = AsyncMock(return_value=False)
        user_repo.create = AsyncMock(return_value=MagicMock(
            id=uuid4(),
            username="admin",
            email="admin@example.com",
            hashed_password="hashed",
            password_change_required=False,
            is_admin=False,
            is_active=True,
            created_at=datetime.now(timezone.utc),
            updated_at=datetime.now(timezone.utc),
        ))
        user_repo.set_admin = AsyncMock(side_effect=lambda uid, val: user_repo.create.return_value)

        user, tokens = await auth_service_open.bootstrap_admin(
            bootstrap_secret="secret123",
            expected_secret="secret123",
            username="admin",
            email="admin@example.com",
            password="password123",
        )

        assert user is not None
        assert isinstance(tokens, AuthTokensResponse)
        assert tokens.access_token is not None
        assert tokens.refresh_token is not None
        user_repo.create.assert_called_once()
        user_repo.set_admin.assert_called_once()

    @pytest.mark.asyncio
    async def test_bootstrap_admin_wrong_secret(self, auth_service_open):
        """Test bootstrap admin with wrong secret."""
        with pytest.raises(ForbiddenError, match="Invalid bootstrap secret"):
            await auth_service_open.bootstrap_admin(
                bootstrap_secret="wrong",
                expected_secret="correct",
                username="admin",
                email="admin@example.com",
                password="password123",
            )

    @pytest.mark.asyncio
    async def test_bootstrap_admin_no_expected_secret(self, auth_service_open):
        """Test bootstrap admin without expected secret."""
        with pytest.raises(ForbiddenError, match="Invalid bootstrap secret"):
            await auth_service_open.bootstrap_admin(
                bootstrap_secret="secret",
                expected_secret=None,
                username="admin",
                email="admin@example.com",
                password="password123",
            )

    @pytest.mark.asyncio
    async def test_bootstrap_admin_already_exists(self, auth_service_open, mock_repos):
        """Test bootstrap admin when admin already exists."""
        user_repo, *_ = mock_repos
        user_repo.count_admins = AsyncMock(return_value=1)

        with pytest.raises(ConflictError, match="Admin user already exists"):
            await auth_service_open.bootstrap_admin(
                bootstrap_secret="secret",
                expected_secret="secret",
                username="admin",
                email="admin@example.com",
                password="password123",
            )

    @pytest.mark.asyncio
    async def test_bootstrap_admin_user_exists(self, auth_service_open, mock_repos):
        """Test bootstrap admin when user already exists."""
        user_repo, *_ = mock_repos
        user_repo.count_admins = AsyncMock(return_value=0)
        user_repo.user_exists = AsyncMock(return_value=True)

        with pytest.raises(UserAlreadyExistsError, match="User with this username or email already exists"):
            await auth_service_open.bootstrap_admin(
                bootstrap_secret="secret",
                expected_secret="secret",
                username="existing",
                email="existing@example.com",
                password="password123",
            )


# ---------------------------------------------------------------------------
# Register Tests
# ---------------------------------------------------------------------------

class TestRegister:
    """Tests for register method."""

    @pytest.mark.asyncio
    async def test_register_success_open_mode(self, auth_service_open, mock_repos):
        """Test successful registration in open mode."""
        user_repo, *_ = mock_repos

        user_repo.user_exists = AsyncMock(return_value=False)
        user_repo.create = AsyncMock(return_value=MagicMock(
            id=uuid4(),
            username="newuser",
            email="new@example.com",
            hashed_password="hashed",
            password_change_required=False,
            is_admin=False,
            is_active=True,
            created_at=datetime.now(timezone.utc),
            updated_at=datetime.now(timezone.utc),
        ))

        user, tokens = await auth_service_open.register(
            username="newuser",
            email="new@example.com",
            password="password123",
        )

        assert user is not None
        assert isinstance(tokens, AuthTokensResponse)
        user_repo.create.assert_called_once()

    @pytest.mark.asyncio
    async def test_register_user_exists(self, auth_service_open, mock_repos):
        """Test registration when user already exists."""
        user_repo, *_ = mock_repos
        user_repo.user_exists = AsyncMock(return_value=True)

        with pytest.raises(UserAlreadyExistsError, match="User with this username or email already exists"):
            await auth_service_open.register(
                username="existing",
                email="existing@example.com",
                password="password123",
            )

    @pytest.mark.asyncio
    async def test_register_invite_mode_success(self, auth_service_invite, mock_repos, sample_invite):
        """Test successful registration in invite mode."""
        user_repo, *_ = mock_repos
        invite_repo = mock_repos[3]

        user_repo.user_exists = AsyncMock(return_value=False)
        user_repo.create = AsyncMock(return_value=MagicMock(
            id=uuid4(),
            username="newuser",
            email="new@example.com",
            hashed_password="hashed",
            password_change_required=False,
            is_admin=False,
            is_active=True,
            created_at=datetime.now(timezone.utc),
            updated_at=datetime.now(timezone.utc),
        ))
        invite_repo.get_by_token = AsyncMock(return_value=sample_invite)
        invite_repo.mark_used = AsyncMock()

        user, tokens = await auth_service_invite.register(
            username="newuser",
            email="new@example.com",
            password="password123",
            invite_token=sample_invite.token,
        )

        assert user is not None
        invite_repo.mark_used.assert_called_once()

    @pytest.mark.asyncio
    async def test_register_invite_mode_missing_token(self, auth_service_invite):
        """Test registration in invite mode without token."""
        with pytest.raises(ForbiddenError, match="Invite token required"):
            await auth_service_invite.register(
                username="newuser",
                email="new@example.com",
                password="password123",
            )

    @pytest.mark.asyncio
    async def test_register_invite_mode_invalid_token(self, auth_service_invite, mock_repos):
        """Test registration in invite mode with invalid token."""
        invite_repo = mock_repos[3]
        invite_repo.get_by_token = AsyncMock(return_value=None)

        with pytest.raises(InvalidTokenError, match="Invalid or expired invite token"):
            await auth_service_invite.register(
                username="newuser",
                email="new@example.com",
                password="password123",
                invite_token=uuid4(),
            )

    @pytest.mark.asyncio
    async def test_register_invite_mode_expired_token(self, auth_service_invite, mock_repos):
        """Test registration in invite mode with expired token."""
        invite_repo = mock_repos[3]
        expired_invite = InviteToken(
            id=uuid4(),
            token=uuid4(),
            created_by=uuid4(),
            email_hint=None,
            expires_at=datetime.now(timezone.utc) - timedelta(hours=1),
            used_at=None,
            used_by=None,
            created_at=datetime.now(timezone.utc),
        )
        invite_repo.get_by_token = AsyncMock(return_value=expired_invite)

        with pytest.raises(InvalidTokenError, match="Invalid or expired invite token"):
            await auth_service_invite.register(
                username="newuser",
                email="new@example.com",
                password="password123",
                invite_token=uuid4(),
            )

    @pytest.mark.asyncio
    async def test_register_invite_mode_no_invite_repo(self, mock_repos):
        """Test registration in invite mode without invite repo."""
        user_repo, revoked_repo, reset_repo, _ = mock_repos
        service = AuthService(
            user_repository=user_repo,
            revoked_repo=revoked_repo,
            reset_repo=reset_repo,
            invite_repo=None,
            registration_mode="invite",
        )

        with pytest.raises(ForbiddenError, match="Invite system is not configured"):
            await service.register(
                username="newuser",
                email="new@example.com",
                password="password123",
                invite_token=uuid4(),
            )


# ---------------------------------------------------------------------------
# Login Tests
# ---------------------------------------------------------------------------

class TestLogin:
    """Tests for login method."""

    @pytest.mark.asyncio
    async def test_login_success(self, auth_service_open, mock_repos, sample_user):
        """Test successful login."""
        user_repo, *_ = mock_repos
        user_repo.get_by_username = AsyncMock(return_value=sample_user)

        with patch("auth_service.services.auth.verify_password", return_value=True):
            user, tokens = await auth_service_open.login("testuser", "password123")

        assert user is not None
        assert isinstance(tokens, AuthTokensResponse)
        user_repo.get_by_username.assert_called_once()

    @pytest.mark.asyncio
    async def test_login_user_not_found(self, auth_service_open, mock_repos):
        """Test login with non-existent user."""
        user_repo, *_ = mock_repos
        user_repo.get_by_username = AsyncMock(return_value=None)

        with pytest.raises(InvalidCredentialsError):
            await auth_service_open.login("nonexistent", "password123")

    @pytest.mark.asyncio
    async def test_login_wrong_password(self, auth_service_open, mock_repos, sample_user):
        """Test login with wrong password."""
        user_repo, *_ = mock_repos
        user_repo.get_by_username = AsyncMock(return_value=sample_user)

        with patch("auth_service.services.auth.verify_password", return_value=False):
            with pytest.raises(InvalidCredentialsError):
                await auth_service_open.login("testuser", "wrongpassword")

    @pytest.mark.asyncio
    async def test_login_inactive_user(self, auth_service_open, mock_repos, sample_user):
        """Test login with inactive user."""
        user_repo, *_ = mock_repos
        inactive_user = User(
            id=sample_user.id,
            username=sample_user.username,
            email=sample_user.email,
            hashed_password=sample_user.hashed_password,
            password_change_required=sample_user.password_change_required,
            is_admin=sample_user.is_admin,
            is_active=False,  # Inactive
            created_at=sample_user.created_at,
            updated_at=sample_user.updated_at,
        )
        user_repo.get_by_username = AsyncMock(return_value=inactive_user)

        with patch("auth_service.services.auth.verify_password", return_value=True):
            with pytest.raises(ForbiddenError, match="Account is deactivated"):
                await auth_service_open.login("testuser", "password123")


# ---------------------------------------------------------------------------
# Token Tests
# ---------------------------------------------------------------------------

class TestTokenOperations:
    """Tests for token operations."""

    @pytest.mark.asyncio
    async def test_logout_success(self, auth_service_open, mock_repos):
        """Test successful logout."""
        _, revoked_repo, *_ = mock_repos

        with patch("auth_service.services.auth.decode_token") as mock_decode:
            mock_decode.return_value = {
                "type": "refresh",
                "jti": str(uuid4()),
                "sub": str(uuid4()),
                "exp": int(datetime.now(timezone.utc).timestamp()) + 3600,
            }
            await auth_service_open.logout("refresh_token")

        revoked_repo.revoke.assert_called_once()

    @pytest.mark.asyncio
    async def test_logout_invalid_token(self, auth_service_open, mock_repos):
        """Test logout with invalid token."""
        _, revoked_repo, *_ = mock_repos

        with patch("auth_service.services.auth.decode_token", side_effect=ValueError("Invalid")):
            with pytest.raises(InvalidCredentialsError):
                await auth_service_open.logout("invalid_token")

        revoked_repo.revoke.assert_not_called()

    @pytest.mark.asyncio
    async def test_logout_wrong_token_type(self, auth_service_open, mock_repos):
        """Test logout with access token instead of refresh."""
        _, revoked_repo, *_ = mock_repos

        with patch("auth_service.services.auth.decode_token") as mock_decode:
            mock_decode.return_value = {
                "type": "access",  # Wrong type
                "jti": str(uuid4()),
                "sub": str(uuid4()),
                "exp": int(datetime.now(timezone.utc).timestamp()) + 3600,
            }
            with pytest.raises(InvalidCredentialsError):
                await auth_service_open.logout("access_token")

        revoked_repo.revoke.assert_not_called()

    @pytest.mark.asyncio
    async def test_refresh_token_success(self, auth_service_open, mock_repos, sample_user):
        """Test successful token refresh."""
        user_repo, revoked_repo, *_ = mock_repos

        with patch("auth_service.services.auth.decode_token") as mock_decode:
            mock_decode.return_value = {
                "type": "refresh",
                "jti": str(uuid4()),
                "sub": str(sample_user.id),
                "exp": int(datetime.now(timezone.utc).timestamp()) + 3600,
            }

            revoked_repo.is_revoked = AsyncMock(return_value=False)
            user_repo.get_by_id = AsyncMock(return_value=sample_user)

            tokens = await auth_service_open.refresh_token("refresh_token")

        assert isinstance(tokens, AuthTokensResponse)
        user_repo.get_by_id.assert_called_once()

    @pytest.mark.asyncio
    async def test_refresh_token_revoked(self, auth_service_open, mock_repos):
        """Test refresh with revoked token."""
        _, revoked_repo, *_ = mock_repos

        with patch("auth_service.services.auth.decode_token") as mock_decode:
            mock_decode.return_value = {
                "type": "refresh",
                "jti": str(uuid4()),
                "sub": str(uuid4()),
                "exp": int(datetime.now(timezone.utc).timestamp()) + 3600,
            }
            revoked_repo.is_revoked = AsyncMock(return_value=True)

            with pytest.raises(InvalidCredentialsError, match="Token has been revoked"):
                await auth_service_open.refresh_token("refresh_token")

    @pytest.mark.asyncio
    async def test_get_user_by_token_success(self, auth_service_open, mock_repos, sample_user):
        """Test successful user retrieval from token."""
        user_repo, *_ = mock_repos

        with patch("auth_service.services.auth.get_user_id_from_token") as mock_get_id:
            mock_get_id.return_value = str(sample_user.id)
            user_repo.get_by_id = AsyncMock(return_value=sample_user)

            user = await auth_service_open.get_user_by_token("access_token")

        assert user == sample_user

    @pytest.mark.asyncio
    async def test_get_user_by_token_invalid_token(self, auth_service_open, mock_repos):
        """Test get_user_by_token with invalid token."""
        user_repo, *_ = mock_repos

        with patch("auth_service.services.auth.get_user_id_from_token", side_effect=ValueError("Invalid")):
            with pytest.raises(InvalidCredentialsError):
                await auth_service_open.get_user_by_token("invalid_token")

        user_repo.get_by_id.assert_not_called()

    @pytest.mark.asyncio
    async def test_get_user_by_token_not_found(self, auth_service_open, mock_repos):
        """Test get_user_by_token for non-existent user."""
        user_repo, *_ = mock_repos

        with patch("auth_service.services.auth.get_user_id_from_token") as mock_get_id:
            mock_get_id.return_value = str(uuid4())
            user_repo.get_by_id = AsyncMock(return_value=None)

            with pytest.raises(UserNotFoundError):
                await auth_service_open.get_user_by_token("access_token")

    @pytest.mark.asyncio
    async def test_get_user_by_token_inactive(self, auth_service_open, mock_repos, sample_user):
        """Test get_user_by_token for inactive user."""
        user_repo, *_ = mock_repos
        inactive_user = User(
            id=sample_user.id,
            username=sample_user.username,
            email=sample_user.email,
            hashed_password=sample_user.hashed_password,
            password_change_required=sample_user.password_change_required,
            is_admin=sample_user.is_admin,
            is_active=False,
            created_at=sample_user.created_at,
            updated_at=sample_user.updated_at,
        )

        with patch("auth_service.services.auth.get_user_id_from_token") as mock_get_id:
            mock_get_id.return_value = str(sample_user.id)
            user_repo.get_by_id = AsyncMock(return_value=inactive_user)

            with pytest.raises(ForbiddenError, match="Account is deactivated"):
                await auth_service_open.get_user_by_token("access_token")


# ---------------------------------------------------------------------------
# Password Tests
# ---------------------------------------------------------------------------

class TestPasswordOperations:
    """Tests for password operations."""

    @pytest.mark.asyncio
    async def test_change_password_success(self, auth_service_open, mock_repos, sample_user):
        """Test successful password change."""
        user_repo, *_ = mock_repos
        user_repo.get_by_id = AsyncMock(return_value=sample_user)
        user_repo.update_password = AsyncMock(return_value=sample_user)

        with patch("auth_service.services.auth.verify_password", return_value=True):
            with patch("auth_service.services.auth.hash_password", return_value="newhash"):
                user = await auth_service_open.change_password(
                    sample_user.id,
                    "oldpassword",
                    "newpassword",
                )

        assert user is not None
        user_repo.update_password.assert_called_once()

    @pytest.mark.asyncio
    async def test_change_password_user_not_found(self, auth_service_open, mock_repos):
        """Test change password for non-existent user."""
        user_repo, *_ = mock_repos
        user_repo.get_by_id = AsyncMock(return_value=None)

        with pytest.raises(UserNotFoundError):
            await auth_service_open.change_password(uuid4(), "old", "new")

    @pytest.mark.asyncio
    async def test_change_password_wrong_old(self, auth_service_open, mock_repos, sample_user):
        """Test change password with wrong old password."""
        user_repo, *_ = mock_repos
        user_repo.get_by_id = AsyncMock(return_value=sample_user)

        with patch("auth_service.services.auth.verify_password", return_value=False):
            with pytest.raises(InvalidCredentialsError, match="Invalid old password"):
                await auth_service_open.change_password(sample_user.id, "wrong", "new")

    @pytest.mark.asyncio
    async def test_request_password_reset_success(self, auth_service_open, mock_repos, sample_user):
        """Test successful password reset request."""
        user_repo, reset_repo = mock_repos[0], mock_repos[2]
        user_repo.get_by_email = AsyncMock(return_value=sample_user)
        reset_repo.create_token = AsyncMock()

        token, expires_at = await auth_service_open.request_password_reset("test@example.com")

        assert token is not None
        assert isinstance(expires_at, datetime)
        reset_repo.create_token.assert_called_once()

    @pytest.mark.asyncio
    async def test_request_password_reset_user_not_found(self, auth_service_open, mock_repos):
        """Test password reset for non-existent user."""
        user_repo, *_ = mock_repos
        user_repo.get_by_email = AsyncMock(return_value=None)

        with pytest.raises(UserNotFoundError):
            await auth_service_open.request_password_reset("nonexistent@example.com")

    @pytest.mark.asyncio
    async def test_confirm_password_reset_success(self, auth_service_open, mock_repos, sample_user):
        """Test successful password reset confirmation."""
        user_repo, reset_repo = mock_repos[0], mock_repos[2]

        reset_record = {
            "user_id": sample_user.id,
            "expires_at": datetime.now(timezone.utc) + timedelta(hours=1),
        }
        reset_repo.get_by_token = AsyncMock(return_value=reset_record)
        user_repo.update_password = AsyncMock(return_value=sample_user)
        user_repo.get_by_id = AsyncMock(return_value=sample_user)

        with patch("auth_service.services.auth.hash_password", return_value="newhash"):
            tokens = await auth_service_open.confirm_password_reset("reset_token", "newpassword")

        assert isinstance(tokens, AuthTokensResponse)
        reset_repo.delete_token.assert_called_once()

    @pytest.mark.asyncio
    async def test_confirm_password_reset_invalid_token(self, auth_service_open, mock_repos):
        """Test password reset with invalid token."""
        reset_repo = mock_repos[2]
        reset_repo.get_by_token = AsyncMock(return_value=None)

        with pytest.raises(InvalidCredentialsError, match="Invalid or expired reset token"):
            await auth_service_open.confirm_password_reset("invalid_token", "newpassword")

    @pytest.mark.asyncio
    async def test_confirm_password_reset_expired(self, auth_service_open, mock_repos, sample_user):
        """Test password reset with expired token."""
        reset_repo = mock_repos[2]
        reset_record = {
            "user_id": sample_user.id,
            "expires_at": datetime.now(timezone.utc) - timedelta(hours=1),
        }
        reset_repo.get_by_token = AsyncMock(return_value=reset_record)

        with pytest.raises(InvalidCredentialsError, match="Invalid or expired reset token"):
            await auth_service_open.confirm_password_reset("expired_token", "newpassword")


# ---------------------------------------------------------------------------
# Admin User Management Tests
# ---------------------------------------------------------------------------

class TestAdminUserManagement:
    """Tests for admin user management."""

    @pytest.mark.asyncio
    async def test_admin_reset_user_success(self, auth_service_open, mock_repos, sample_admin, sample_user):
        """Test admin successfully resets user password."""
        user_repo, *_ = mock_repos
        user_repo.get_by_id = AsyncMock(side_effect=[sample_admin, sample_user])
        user_repo.update_password = AsyncMock(return_value=sample_user)

        with patch("auth_service.services.auth.get_user_id_from_token", return_value=str(sample_admin.id)):
            updated_user, pwd = await auth_service_open.admin_reset_user(
                requester_token="admin_token",
                target_user_id=sample_user.id,
                new_password="newpass123",
            )

        assert updated_user is not None
        assert pwd is not None
        user_repo.update_password.assert_called_once()

    @pytest.mark.asyncio
    async def test_admin_reset_user_generates_password(self, auth_service_open, mock_repos, sample_admin, sample_user):
        """Test admin reset generates password when not provided."""
        user_repo, *_ = mock_repos
        user_repo.get_by_id = AsyncMock(side_effect=[sample_admin, sample_user])
        user_repo.update_password = AsyncMock(return_value=sample_user)

        with patch("auth_service.services.auth.get_user_id_from_token", return_value=str(sample_admin.id)):
            updated_user, pwd = await auth_service_open.admin_reset_user(
                requester_token="admin_token",
                target_user_id=sample_user.id,
                new_password=None,
            )

        assert pwd is not None
        assert pwd.startswith("Tmp1")

    @pytest.mark.asyncio
    async def test_admin_reset_user_not_admin(self, auth_service_open, mock_repos, sample_user):
        """Test non-admin cannot reset user password."""
        user_repo, *_ = mock_repos
        user_repo.get_by_id = AsyncMock(return_value=sample_user)

        with patch("auth_service.services.auth.get_user_id_from_token", return_value=str(sample_user.id)):
            with pytest.raises(ForbiddenError):
                await auth_service_open.admin_reset_user(
                    requester_token="user_token",
                    target_user_id=sample_user.id,
                    new_password="newpass",
                )

    @pytest.mark.asyncio
    async def test_admin_reset_user_not_found(self, auth_service_open, mock_repos, sample_admin):
        """Test admin reset for non-existent user."""
        user_repo, *_ = mock_repos
        user_repo.get_by_id = AsyncMock(side_effect=[sample_admin, None])

        with patch("auth_service.services.auth.get_user_id_from_token", return_value=str(sample_admin.id)):
            with pytest.raises(UserNotFoundError):
                await auth_service_open.admin_reset_user(
                    requester_token="admin_token",
                    target_user_id=uuid4(),
                    new_password="newpass",
                )

    @pytest.mark.asyncio
    async def test_list_users_success(self, auth_service_open, mock_repos, sample_admin):
        """Test admin successfully lists users."""
        user_repo, *_ = mock_repos
        user_repo.get_by_id = AsyncMock(return_value=sample_admin)
        user_repo.list_all = AsyncMock(return_value=[])

        with patch("auth_service.services.auth.get_user_id_from_token", return_value=str(sample_admin.id)):
            users = await auth_service_open.list_users("admin_token", search="test")

        assert isinstance(users, list)
        user_repo.list_all.assert_called_once_with("test")

    @pytest.mark.asyncio
    async def test_list_users_not_admin(self, auth_service_open, mock_repos, sample_user):
        """Test non-admin cannot list users."""
        user_repo, *_ = mock_repos
        user_repo.get_by_id = AsyncMock(return_value=sample_user)

        with patch("auth_service.services.auth.get_user_id_from_token", return_value=str(sample_user.id)):
            with pytest.raises(ForbiddenError):
                await auth_service_open.list_users("user_token")

    @pytest.mark.asyncio
    async def test_update_user_success(self, auth_service_open, mock_repos, sample_admin, sample_user):
        """Test admin successfully updates user."""
        user_repo, *_ = mock_repos
        user_repo.get_by_id = AsyncMock(side_effect=[sample_admin, sample_user])
        user_repo.set_active = AsyncMock(return_value=sample_user)
        user_repo.count_admins = AsyncMock(return_value=2)

        with patch("auth_service.services.auth.get_user_id_from_token", return_value=str(sample_admin.id)):
            updated = await auth_service_open.update_user(
                requester_token="admin_token",
                target_user_id=sample_user.id,
                is_active=True,
                is_admin=None,
            )

        assert updated is not None
        user_repo.set_active.assert_called_once()

    @pytest.mark.asyncio
    async def test_update_user_cannot_modify_self(self, auth_service_open, mock_repos, sample_admin):
        """Test admin cannot modify own account."""
        user_repo, *_ = mock_repos
        user_repo.get_by_id = AsyncMock(return_value=sample_admin)

        with patch("auth_service.services.auth.get_user_id_from_token", return_value=str(sample_admin.id)):
            with pytest.raises(ForbiddenError, match="Cannot modify your own account"):
                await auth_service_open.update_user(
                    requester_token="admin_token",
                    target_user_id=sample_admin.id,
                    is_active=True,
                    is_admin=None,
                )

    @pytest.mark.asyncio
    async def test_update_user_last_admin(self, auth_service_open, mock_repos, sample_admin, sample_user):
        """Test cannot demote last admin."""
        user_repo, *_ = mock_repos
        user_repo.get_by_id = AsyncMock(side_effect=[sample_admin, sample_user])
        user_repo.count_admins = AsyncMock(return_value=1)

        with patch("auth_service.services.auth.get_user_id_from_token", return_value=str(sample_admin.id)):
            with pytest.raises(ConflictError, match="Cannot remove the last admin"):
                await auth_service_open.update_user(
                    requester_token="admin_token",
                    target_user_id=sample_user.id,
                    is_active=None,
                    is_admin=False,
                )

    @pytest.mark.asyncio
    async def test_delete_user_success(self, auth_service_open, mock_repos, sample_admin, sample_user):
        """Test admin successfully deletes user."""
        user_repo, *_ = mock_repos
        user_repo.get_by_id = AsyncMock(side_effect=[sample_admin, sample_user])
        user_repo.delete = AsyncMock(return_value=True)
        user_repo.count_admins = AsyncMock(return_value=2)

        with patch("auth_service.services.auth.get_user_id_from_token", return_value=str(sample_admin.id)):
            result = await auth_service_open.delete_user(
                requester_token="admin_token",
                target_user_id=sample_user.id,
            )

        assert result is True
        user_repo.delete.assert_called_once()

    @pytest.mark.asyncio
    async def test_delete_user_cannot_delete_self(self, auth_service_open, mock_repos, sample_admin):
        """Test admin cannot delete own account."""
        user_repo, *_ = mock_repos
        user_repo.get_by_id = AsyncMock(return_value=sample_admin)

        with patch("auth_service.services.auth.get_user_id_from_token", return_value=str(sample_admin.id)):
            with pytest.raises(ForbiddenError, match="Cannot delete your own account"):
                await auth_service_open.delete_user(
                    requester_token="admin_token",
                    target_user_id=sample_admin.id,
                )

    @pytest.mark.asyncio
    async def test_delete_user_last_admin(self, auth_service_open, mock_repos, sample_admin, sample_user):
        """Test cannot delete last admin."""
        user_repo, *_ = mock_repos
        user_repo.get_by_id = AsyncMock(side_effect=[sample_admin, sample_user])
        user_repo.count_admins = AsyncMock(return_value=1)

        with patch("auth_service.services.auth.get_user_id_from_token", return_value=str(sample_admin.id)):
            with pytest.raises(ConflictError, match="Cannot delete the last admin"):
                await auth_service_open.delete_user(
                    requester_token="admin_token",
                    target_user_id=sample_user.id,
                )

    @pytest.mark.asyncio
    async def test_delete_user_not_found(self, auth_service_open, mock_repos, sample_admin):
        """Test delete non-existent user."""
        user_repo, *_ = mock_repos
        user_repo.get_by_id = AsyncMock(side_effect=[sample_admin, None])

        with patch("auth_service.services.auth.get_user_id_from_token", return_value=str(sample_admin.id)):
            with pytest.raises(NotFoundError, match="User not found"):
                await auth_service_open.delete_user(
                    requester_token="admin_token",
                    target_user_id=uuid4(),
                )


# ---------------------------------------------------------------------------
# Invite Management Tests
# ---------------------------------------------------------------------------

class TestInviteManagement:
    """Tests for invite management."""

    @pytest.mark.asyncio
    async def test_create_invite_success(self, auth_service_open, mock_repos, sample_admin):
        """Test admin successfully creates invite."""
        user_repo, _, _, invite_repo = mock_repos
        user_repo.get_by_id = AsyncMock(return_value=sample_admin)
        invite_repo.create = AsyncMock(return_value=MagicMock(
            id=uuid4(),
            token=uuid4(),
            created_by=sample_admin.id,
            email_hint="test@example.com",
            expires_at=datetime.now(timezone.utc) + timedelta(hours=24),
            used_at=None,
            used_by=None,
            created_at=datetime.now(timezone.utc),
        ))

        with patch("auth_service.services.auth.get_user_id_from_token", return_value=str(sample_admin.id)):
            invite = await auth_service_open.create_invite(
                requester_token="admin_token",
                email_hint="test@example.com",
                expires_in_hours=24,
            )

        assert invite is not None
        invite_repo.create.assert_called_once()

    @pytest.mark.asyncio
    async def test_create_invite_not_admin(self, auth_service_open, mock_repos, sample_user):
        """Test non-admin cannot create invite."""
        user_repo, *_ = mock_repos
        user_repo.get_by_id = AsyncMock(return_value=sample_user)

        with patch("auth_service.services.auth.get_user_id_from_token", return_value=str(sample_user.id)):
            with pytest.raises(ForbiddenError):
                await auth_service_open.create_invite(
                    requester_token="user_token",
                    email_hint="test@example.com",
                    expires_in_hours=24,
                )

    @pytest.mark.asyncio
    async def test_list_invites_success(self, auth_service_open, mock_repos, sample_admin):
        """Test admin successfully lists invites."""
        user_repo, _, _, invite_repo = mock_repos
        user_repo.get_by_id = AsyncMock(return_value=sample_admin)
        invite_repo.list_all = AsyncMock(return_value=[])

        with patch("auth_service.services.auth.get_user_id_from_token", return_value=str(sample_admin.id)):
            invites = await auth_service_open.list_invites("admin_token", active_only=True)

        assert isinstance(invites, list)
        invite_repo.list_all.assert_called_once_with(active_only=True)

    @pytest.mark.asyncio
    async def test_revoke_invite_success(self, auth_service_open, mock_repos, sample_admin, sample_invite):
        """Test admin successfully revokes invite."""
        user_repo, _, _, invite_repo = mock_repos
        user_repo.get_by_id = AsyncMock(return_value=sample_admin)
        invite_repo.get_by_token = AsyncMock(return_value=sample_invite)
        invite_repo.delete = AsyncMock(return_value=True)

        with patch("auth_service.services.auth.get_user_id_from_token", return_value=str(sample_admin.id)):
            result = await auth_service_open.revoke_invite("admin_token", sample_invite.token)

        assert result is True
        invite_repo.delete.assert_called_once()

    @pytest.mark.asyncio
    async def test_revoke_invite_used(self, auth_service_open, mock_repos, sample_admin):
        """Test cannot revoke used invite."""
        user_repo, _, _, invite_repo = mock_repos
        user_repo.get_by_id = AsyncMock(return_value=sample_admin)

        used_invite = InviteToken(
            id=uuid4(),
            token=uuid4(),
            created_by=uuid4(),
            email_hint=None,
            expires_at=datetime.now(timezone.utc) + timedelta(hours=24),
            used_at=datetime.now(timezone.utc),
            used_by=uuid4(),
            created_at=datetime.now(timezone.utc),
        )
        invite_repo.get_by_token = AsyncMock(return_value=used_invite)

        with patch("auth_service.services.auth.get_user_id_from_token", return_value=str(sample_admin.id)):
            with pytest.raises(ConflictError, match="Cannot revoke a used invite token"):
                await auth_service_open.revoke_invite("admin_token", uuid4())

    @pytest.mark.asyncio
    async def test_revoke_invite_not_found(self, auth_service_open, mock_repos, sample_admin):
        """Test revoke non-existent invite."""
        user_repo, _, _, invite_repo = mock_repos
        user_repo.get_by_id = AsyncMock(return_value=sample_admin)
        invite_repo.get_by_token = AsyncMock(return_value=None)

        with patch("auth_service.services.auth.get_user_id_from_token", return_value=str(sample_admin.id)):
            with pytest.raises(NotFoundError, match="Invite token not found"):
                await auth_service_open.revoke_invite("admin_token", uuid4())


# ---------------------------------------------------------------------------
# Integration Tests
# ---------------------------------------------------------------------------

class TestAuthIntegration:
    """Integration tests for auth service."""

    @pytest.mark.asyncio
    async def test_full_registration_login_flow(self, auth_service_open, mock_repos):
        """Test complete registration and login flow."""
        user_repo, *_ = mock_repos

        # Registration
        user_repo.user_exists = AsyncMock(return_value=False)
        created_user = MagicMock(
            id=uuid4(),
            username="newuser",
            email="new@example.com",
            hashed_password="hashed",
            password_change_required=False,
            is_admin=False,
            is_active=True,
            created_at=datetime.now(timezone.utc),
            updated_at=datetime.now(timezone.utc),
        )
        user_repo.create = AsyncMock(return_value=created_user)

        user, tokens = await auth_service_open.register(
            username="newuser",
            email="new@example.com",
            password="password123",
        )

        assert user is not None
        assert tokens.access_token is not None

        # Login
        user_repo.get_by_username = AsyncMock(return_value=created_user)

        with patch("auth_service.services.auth.verify_password", return_value=True):
            logged_user, logged_tokens = await auth_service_open.login("newuser", "password123")

        assert logged_user is not None
        assert logged_tokens.access_token is not None

    @pytest.mark.asyncio
    async def test_password_change_flow(self, auth_service_open, mock_repos, sample_user):
        """Test password change flow."""
        user_repo, *_ = mock_repos
        user_repo.get_by_id = AsyncMock(return_value=sample_user)
        user_repo.update_password = AsyncMock(return_value=sample_user)

        with patch("auth_service.services.auth.verify_password", return_value=True):
            with patch("auth_service.services.auth.hash_password", return_value="newhash"):
                user = await auth_service_open.change_password(
                    sample_user.id,
                    "oldpassword",
                    "newpassword",
                )

        assert user is not None

    @pytest.mark.asyncio
    async def test_password_reset_flow(self, auth_service_open, mock_repos, sample_user):
        """Test complete password reset flow."""
        user_repo, reset_repo = mock_repos[0], mock_repos[2]
        user_repo.get_by_email = AsyncMock(return_value=sample_user)
        reset_repo.create_token = AsyncMock()

        # Request reset
        token, expires_at = await auth_service_open.request_password_reset("test@example.com")
        assert token is not None

        # Confirm reset
        reset_record = {
            "user_id": sample_user.id,
            "expires_at": datetime.now(timezone.utc) + timedelta(hours=1),
        }
        reset_repo.get_by_token = AsyncMock(return_value=reset_record)
        user_repo.update_password = AsyncMock(return_value=sample_user)
        user_repo.get_by_id = AsyncMock(return_value=sample_user)

        with patch("auth_service.services.auth.hash_password", return_value="newhash"):
            tokens = await auth_service_open.confirm_password_reset(token, "newpassword")

        assert isinstance(tokens, AuthTokensResponse)
