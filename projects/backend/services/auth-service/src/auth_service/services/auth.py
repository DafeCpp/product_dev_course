"""Authentication service."""
from __future__ import annotations

import secrets
import structlog
from datetime import datetime, timedelta, timezone
from uuid import UUID

from auth_service.core.exceptions import (
    ConflictError,
    ForbiddenError,
    InvalidCredentialsError,
    InvalidTokenError,
    NotFoundError,
    UserAlreadyExistsError,
    UserNotFoundError,
)
from auth_service.domain.dto import AuthTokensResponse, UserResponse
from auth_service.domain.models import AuditAction, InviteToken, ScopeType, User
from auth_service.repositories.audit import AuditRepository
from auth_service.repositories.invites import InviteRepository
from auth_service.repositories.password_reset import PasswordResetRepository
from auth_service.repositories.revoked_tokens import RevokedTokenRepository
from auth_service.repositories.token_families import TokenFamilyRepository
from auth_service.repositories.users import UserRepository
from auth_service.services.jwt import (
    create_access_token,
    create_refresh_token,
    decode_token,
    get_user_id_from_token,
)
from auth_service.services.password import hash_password, verify_password
from auth_service.services.permission import PermissionService
from auth_service.prometheus_metrics import (
    AUTH_LOGINS,
    AUTH_REUSE_DETECTIONS,
    AUTH_TOKEN_REFRESHES,
)

logger = structlog.get_logger(__name__)


class AuthService:
    """Service for authentication operations."""

    def __init__(
        self,
        user_repository: UserRepository,
        revoked_repo: RevokedTokenRepository,
        reset_repo: PasswordResetRepository,
        permission_service: PermissionService,
        invite_repo: InviteRepository | None = None,
        registration_mode: str = "open",
        audit_repo: AuditRepository | None = None,
        family_repo: TokenFamilyRepository | None = None,
    ):
        self._user_repo = user_repository
        self._revoked_repo = revoked_repo
        self._reset_repo = reset_repo
        self._perm_svc = permission_service
        self._invite_repo = invite_repo
        self._registration_mode = registration_mode
        self._audit_repo = audit_repo
        self._family_repo = family_repo

    async def _audit(
        self,
        actor_id: UUID,
        action: str,
        *,
        target_type: str | None = None,
        target_id: str | None = None,
        details: dict | None = None,
        ip_address: str | None = None,
        user_agent: str | None = None,
    ) -> None:
        """Write audit entry, silently ignoring errors."""
        if self._audit_repo is None:
            return
        try:
            await self._audit_repo.log(
                actor_id=actor_id,
                action=action,
                scope_type=ScopeType.SYSTEM,
                target_type=target_type,
                target_id=target_id,
                details=details,
                ip_address=ip_address,
                user_agent=user_agent,
            )
        except Exception as e:
            logger.warning("Audit log write failed", action=action, error=str(e))

    async def bootstrap_admin(
        self,
        bootstrap_secret: str,
        expected_secret: str | None,
        username: str,
        email: str,
        password: str,
        ip_address: str | None = None,
        user_agent: str | None = None,
    ) -> tuple[User, AuthTokensResponse]:
        """Create the first superadmin user. Only works if no superadmin exists yet."""
        if not expected_secret or bootstrap_secret != expected_secret:
            raise ForbiddenError("Invalid bootstrap secret")

        count = await self._perm_svc.count_superadmins()
        if count > 0:
            raise ConflictError("Admin user already exists")

        if await self._user_repo.user_exists(username, email):
            raise UserAlreadyExistsError("User with this username or email already exists")

        hashed_pw = hash_password(password)
        user = await self._user_repo.create(username, email, hashed_pw, password_change_required=False)

        # Grant superadmin system role (self-grant for bootstrap, no grantor check)
        await self._perm_svc.bootstrap_grant_superadmin(user.id)

        tokens = await self._create_tokens(str(user.id))
        await self._audit(
            user.id, AuditAction.BOOTSTRAP,
            target_type="user", target_id=str(user.id),
            details={"username": username},
            ip_address=ip_address, user_agent=user_agent,
        )
        return user, tokens

    async def register(
        self,
        username: str,
        email: str,
        password: str,
        invite_token: UUID | None = None,
        ip_address: str | None = None,
        user_agent: str | None = None,
    ) -> tuple[User, AuthTokensResponse]:
        """Register a new user."""
        validated_invite = None

        if self._registration_mode == "invite":
            if self._invite_repo is None:
                raise ForbiddenError("Invite system is not configured")
            if invite_token is None:
                raise ForbiddenError("Invite token required")

        # Validate invite if provided (regardless of registration mode), so it gets marked used.
        if invite_token is not None and self._invite_repo is not None:
            validated_invite = await self._invite_repo.get_by_token(invite_token)
            if not validated_invite or not validated_invite.is_active:
                raise InvalidTokenError("Invalid or expired invite token")

        if await self._user_repo.user_exists(username, email):
            raise UserAlreadyExistsError("User with this username or email already exists")

        hashed_pw = hash_password(password)
        user = await self._user_repo.create(username, email, hashed_pw, password_change_required=False)

        if validated_invite is not None and self._invite_repo is not None and invite_token is not None:
            await self._invite_repo.mark_used(invite_token, user.id)

        tokens = await self._create_tokens(str(user.id))
        await self._audit(
            user.id, AuditAction.REGISTER,
            target_type="user", target_id=str(user.id),
            details={"username": username},
            ip_address=ip_address, user_agent=user_agent,
        )
        return user, tokens

    async def create_invite(
        self,
        requester_id: UUID,
        email_hint: str | None,
        expires_in_hours: int,
    ) -> InviteToken:
        """Create a new invite token. Requires 'users.create' permission."""
        await self._perm_svc.ensure_permission(requester_id, "users.create")
        if self._invite_repo is None:
            raise ForbiddenError("Invite system is not configured")

        expires_at = datetime.now(timezone.utc) + timedelta(hours=expires_in_hours)
        return await self._invite_repo.create(requester_id, email_hint, expires_at)

    async def list_invites(
        self,
        requester_id: UUID,
        active_only: bool = False,
    ) -> list[InviteToken]:
        """List invite tokens. Requires 'users.create' permission."""
        await self._perm_svc.ensure_permission(requester_id, "users.create")
        if self._invite_repo is None:
            raise ForbiddenError("Invite system is not configured")

        return await self._invite_repo.list_all(active_only=active_only)

    async def revoke_invite(self, requester_id: UUID, token: UUID) -> bool:
        """Revoke (delete) an invite token. Requires 'users.create'. Cannot revoke used tokens."""
        await self._perm_svc.ensure_permission(requester_id, "users.create")
        if self._invite_repo is None:
            raise ForbiddenError("Invite system is not configured")

        invite = await self._invite_repo.get_by_token(token)
        if not invite:
            raise NotFoundError("Invite token not found")
        if invite.used_at is not None:
            raise ConflictError("Cannot revoke a used invite token")

        return await self._invite_repo.delete(token)

    async def login(
        self,
        username: str,
        password: str,
        ip_address: str | None = None,
        user_agent: str | None = None,
    ) -> tuple[User, AuthTokensResponse]:
        """Authenticate user and return tokens."""
        user = await self._user_repo.get_by_username(username)
        if not user:
            AUTH_LOGINS.labels(result="failure").inc()
            raise InvalidCredentialsError()

        if not verify_password(password, user.hashed_password):
            AUTH_LOGINS.labels(result="failure").inc()
            raise InvalidCredentialsError()

        if not user.is_active:
            raise ForbiddenError("Account is deactivated")

        tokens = await self._create_tokens(
            str(user.id),
            password_change_required=user.password_change_required,
        )
        await self._audit(
            user.id, AuditAction.LOGIN,
            ip_address=ip_address, user_agent=user_agent,
        )
        AUTH_LOGINS.labels(result="success").inc()
        return user, tokens

    async def change_password(
        self,
        user_id: UUID,
        old_password: str,
        new_password: str,
        ip_address: str | None = None,
        user_agent: str | None = None,
    ) -> tuple[User, AuthTokensResponse]:
        """Change user password.

        Returns the updated user and fresh tokens without the ``pcr`` claim so
        the caller is not locked out immediately after changing the password.
        """
        user = await self._user_repo.get_by_id(user_id)
        if not user:
            raise UserNotFoundError()

        if not verify_password(old_password, user.hashed_password):
            raise InvalidCredentialsError("Invalid old password")

        new_hashed = hash_password(new_password)
        updated = await self._user_repo.update_password(
            user_id, new_hashed, password_change_required=False,
        )
        # Invalidate all refresh token families on password change
        if self._family_repo is not None:
            await self._family_repo.revoke_all_user_families(user_id)
        await self._audit(
            user_id, AuditAction.PASSWORD_CHANGE,
            target_type="user", target_id=str(user_id),
            ip_address=ip_address, user_agent=user_agent,
        )
        # Issue fresh tokens without pcr so the client is not blocked
        tokens = await self._create_tokens(str(user_id), password_change_required=False)
        return updated, tokens

    async def logout(
        self,
        refresh_token: str,
        ip_address: str | None = None,
        user_agent: str | None = None,
    ) -> None:
        """Revoke a refresh token and its family (if present)."""
        try:
            payload = decode_token(refresh_token)
            if payload.get("type") != "refresh":
                raise ValueError("Invalid token type")
            jti = payload.get("jti")
            if not jti:
                raise ValueError("Token missing jti")
            user_id = payload.get("sub")
            if not user_id:
                raise ValueError("Token missing user ID")
            exp: int = payload["exp"]
        except ValueError as e:
            raise InvalidCredentialsError(str(e)) from e

        fid_str: str | None = payload.get("fid")
        family_id: UUID | None = UUID(fid_str) if fid_str else None

        expires_at = datetime.fromtimestamp(exp, tz=timezone.utc)
        await self._revoked_repo.revoke(UUID(jti), UUID(user_id), expires_at, family_id)

        if family_id is not None and self._family_repo is not None:
            await self._family_repo.revoke_family(family_id)

        await self._audit(
            UUID(user_id), AuditAction.LOGOUT,
            ip_address=ip_address, user_agent=user_agent,
        )

    async def refresh_token(self, refresh_token: str) -> AuthTokensResponse:
        """Refresh access token using refresh token (with rotation).

        Flow:
        1. Decode and validate JWT claims.
        2. If family tracking is enabled (fid claim present):
           a. Check family is not revoked (stolen token detector).
           b. Check jti is not in revoked_tokens (reuse detection).
              If jti already revoked → whole family is compromised → revoke family → 401.
           c. Revoke this jti (add to blacklist).
           d. Issue new refresh token with the *same* family_id.
        3. Issue new access token.
        """
        try:
            payload = decode_token(refresh_token)
            if payload.get("type") != "refresh":
                raise ValueError("Invalid token type")
            jti = payload.get("jti")
            if not jti:
                raise ValueError("Token missing jti")
            user_id = payload.get("sub")
            if not user_id:
                raise ValueError("Token missing user ID")
            exp: int = payload["exp"]
        except ValueError as e:
            raise InvalidCredentialsError(str(e)) from e

        fid_str: str | None = payload.get("fid")
        family_id: UUID | None = UUID(fid_str) if fid_str else None

        # Family-level check: if the whole family was revoked (e.g. logout / password change)
        if family_id is not None and self._family_repo is not None:
            if await self._family_repo.is_revoked(family_id):
                raise InvalidCredentialsError("Token family has been revoked")

        # Per-token check: if this specific jti was already used → reuse detection
        if await self._revoked_repo.is_revoked(UUID(jti)):
            # Token reuse detected — revoke the whole family to protect the user
            AUTH_REUSE_DETECTIONS.inc()
            if family_id is not None and self._family_repo is not None:
                await self._family_repo.revoke_family(family_id)
            raise InvalidCredentialsError("Token has been revoked")

        user = await self._user_repo.get_by_id(UUID(user_id))
        if not user:
            raise UserNotFoundError()

        # Retire the current jti (mark as used)
        expires_at = datetime.fromtimestamp(exp, tz=timezone.utc)
        await self._revoked_repo.revoke(UUID(jti), UUID(user_id), expires_at, family_id)

        # Issue new tokens, carrying the same family_id forward
        tokens = await self._create_tokens(user_id, family_id=family_id)
        AUTH_TOKEN_REFRESHES.inc()
        return tokens

    async def get_user_by_token(self, access_token: str) -> User:
        """Get user from access token."""
        try:
            user_id = get_user_id_from_token(access_token)
        except ValueError as e:
            raise InvalidCredentialsError(str(e)) from e

        user = await self._user_repo.get_by_id(UUID(user_id))
        if not user:
            raise UserNotFoundError()

        if not user.is_active:
            raise ForbiddenError("Account is deactivated")

        return user

    async def request_password_reset(self, email: str) -> tuple[str, datetime]:
        """Generate a password reset token for the given email."""
        user = await self._user_repo.get_by_email(email)
        if not user:
            raise UserNotFoundError()

        token = secrets.token_urlsafe(32)
        expires_at = datetime.now(timezone.utc) + timedelta(hours=1)
        await self._reset_repo.create_token(token, user.id, expires_at)
        return token, expires_at

    async def confirm_password_reset(
        self,
        token: str,
        new_password: str,
        ip_address: str | None = None,
        user_agent: str | None = None,
    ) -> AuthTokensResponse:
        """Reset password using a valid reset token."""
        record = await self._reset_repo.get_by_token(token)
        if not record:
            raise InvalidCredentialsError("Invalid or expired reset token")

        if record["expires_at"] < datetime.now(timezone.utc):
            raise InvalidCredentialsError("Reset token expired")

        user_id: UUID = record["user_id"]
        await self._user_repo.update_password(user_id, hash_password(new_password), password_change_required=False)
        await self._reset_repo.delete_token(token)
        tokens = await self._create_tokens(str(user_id))
        await self._audit(
            user_id, AuditAction.PASSWORD_RESET,
            target_type="user", target_id=str(user_id),
            ip_address=ip_address, user_agent=user_agent,
        )
        return tokens

    async def admin_reset_user(
        self,
        requester_id: UUID,
        target_user_id: UUID,
        new_password: str | None,
    ) -> tuple[User, str]:
        """Admin resets another user's password. Requires 'users.reset_password'."""
        await self._perm_svc.ensure_permission(requester_id, "users.reset_password")

        target = await self._user_repo.get_by_id(target_user_id)
        if not target:
            raise UserNotFoundError()

        pwd = new_password if new_password else ("Tmp1" + secrets.token_hex(8))
        updated_user = await self._user_repo.update_password(
            target_user_id, hash_password(pwd), password_change_required=True,
        )
        return updated_user, pwd

    async def list_users(
        self,
        requester_id: UUID,
        search: str | None = None,
    ) -> list[User]:
        """List all users. Requires 'users.list' permission."""
        await self._perm_svc.ensure_permission(requester_id, "users.list")
        return await self._user_repo.list_all(search)

    async def update_user(
        self,
        requester_id: UUID,
        target_user_id: UUID,
        is_active: bool | None,
        is_admin: bool | None = None,
    ) -> User:
        """Update user flags. Requires 'users.update' permission."""
        await self._perm_svc.ensure_permission(requester_id, "users.update")

        if requester_id == target_user_id:
            raise ForbiddenError("Cannot modify your own account")

        target = await self._user_repo.get_by_id(target_user_id)
        if not target:
            raise UserNotFoundError()

        if is_active is not None:
            if not is_active and await self._perm_svc.is_superadmin(target_user_id):
                count = await self._perm_svc.count_superadmins()
                if count <= 1:
                    raise ConflictError("Cannot remove the last superadmin")
            target = await self._user_repo.set_active(target_user_id, is_active)

        if is_admin is not None:
            await self._perm_svc.set_admin_role(
                requester_id, target_user_id, grant=is_admin,
            )

        return target

    async def delete_user(
        self,
        requester_id: UUID,
        target_user_id: UUID,
    ) -> bool:
        """Delete a user. Requires 'users.delete' permission."""
        await self._perm_svc.ensure_permission(requester_id, "users.delete")

        if requester_id == target_user_id:
            raise ForbiddenError("Cannot delete your own account")

        target = await self._user_repo.get_by_id(target_user_id)
        if not target:
            raise NotFoundError("User not found")

        # Protect last superadmin
        is_sa = await self._perm_svc.is_superadmin(target_user_id)
        if is_sa:
            count = await self._perm_svc.count_superadmins()
            if count <= 1:
                raise ConflictError("Cannot delete the last superadmin")

        deleted = await self._user_repo.delete(target_user_id)
        if not deleted:
            raise NotFoundError("User not found")
        return True

    async def get_user_response(self, user: User) -> UserResponse:
        """Build UserResponse with system role names."""
        role_names = await self._perm_svc.list_system_role_names(user.id)
        return UserResponse.from_user(user, system_roles=role_names)

    async def get_user_responses(self, users: list[User]) -> list[UserResponse]:
        """Build UserResponse list with system role names in a single batch query."""
        if not users:
            return []
        roles_by_user = await self._perm_svc.batch_list_system_role_names(
            [u.id for u in users],
        )
        return [
            UserResponse.from_user(u, system_roles=roles_by_user.get(u.id, []))
            for u in users
        ]

    async def _create_tokens(
        self,
        user_id: str,
        family_id: UUID | None = None,
        password_change_required: bool = False,
    ) -> AuthTokensResponse:
        """Create access and refresh tokens with RBAC v2 claims.

        Args:
            user_id: User identifier string.
            family_id: Existing family to continue (refresh rotation).
                When ``None`` and ``_family_repo`` is configured, a new family
                is created so the issued refresh token can be tracked.
            password_change_required: When True the ``pcr: true`` claim is
                embedded in the access token.
        """
        uid = UUID(user_id)
        is_sa = await self._perm_svc.is_superadmin(uid)

        # Get system permissions (empty if superadmin)
        system_perms: list[str] = []
        if not is_sa:
            effective = await self._perm_svc.get_effective_permissions(uid)
            system_perms = effective.system_permissions

        # Create a new family when we are issuing a brand-new refresh token
        if family_id is None and self._family_repo is not None:
            family_id = await self._family_repo.create(uid)

        fid_str: str | None = str(family_id) if family_id is not None else None

        return AuthTokensResponse(
            access_token=create_access_token(
                user_id,
                is_superadmin=is_sa,
                system_permissions=system_perms,
                password_change_required=password_change_required,
            ),
            refresh_token=create_refresh_token(user_id, family_id=fid_str),
            password_change_required=password_change_required,
        )
