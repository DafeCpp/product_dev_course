"""AuthService and auth repository branches that the HTTP-level tests cannot reach."""
from __future__ import annotations

import time
from datetime import UTC, datetime, timedelta
from uuid import UUID, uuid4

import asyncpg
import jwt
import pytest

from auth_service.core.exceptions import (
    ConflictError,
    ForbiddenError,
    InvalidCredentialsError,
    InvalidTokenError,
    NotFoundError,
    UserNotFoundError,
)
from auth_service.domain.models import ADMIN_ROLE_ID, SUPERADMIN_ROLE_ID
from auth_service.repositories.invites import InviteRepository
from auth_service.repositories.password_reset import PasswordResetRepository
from auth_service.repositories.permissions import PermissionRepository
from auth_service.repositories.revoked_tokens import RevokedTokenRepository
from auth_service.repositories.roles import RoleRepository
from auth_service.repositories.token_families import TokenFamilyRepository
from auth_service.repositories.user_roles import UserRoleRepository
from auth_service.repositories.users import UserRepository
from auth_service.services.auth import AuthService
from auth_service.services.jwt import create_access_token, create_refresh_token
from auth_service.services.permission import PermissionService
from auth_service.settings import settings
from auth_service.workers.token_cleanup import token_cleanup

PASSWORD = "Sup3r-secret"


def build_service(pool, *, invites: bool = True, families: bool = True, **overrides) -> AuthService:
    perm = PermissionService(
        PermissionRepository(pool), RoleRepository(pool), UserRoleRepository(pool)
    )
    return AuthService(
        overrides.get("user_repo") or UserRepository(pool),
        RevokedTokenRepository(pool),
        PasswordResetRepository(pool),
        perm,
        invite_repo=(overrides.get("invite_repo") or InviteRepository(pool)) if invites else None,
        registration_mode=overrides.get("registration_mode", "open"),
        family_repo=TokenFamilyRepository(pool) if families else None,
    )


@pytest.fixture
async def service(pool_with_seed):
    return build_service(pool_with_seed)


@pytest.fixture
async def make_user(service):
    async def create(name: str, *system_roles: UUID):
        user, _ = await service.register(name, f"{name}@example.com", PASSWORD)
        pool = service._user_repo._pool
        for role_id in system_roles:
            await pool.execute(
                "INSERT INTO user_system_roles (user_id, role_id, granted_by, granted_at) "
                "VALUES ($1, $2, $1, now())",
                user.id,
                role_id,
            )
        return user

    return create


def encode(payload: dict) -> str:
    return jwt.encode(payload, settings.jwt_secret, algorithm=settings.jwt_algorithm)


def refresh_claims(**changes) -> dict:
    now = int(time.time())
    claims = {
        "sub": str(uuid4()),
        "type": "refresh",
        "jti": str(uuid4()),
        "iat": now,
        "exp": now + 600,
    }
    claims.update(changes)
    return {k: v for k, v in claims.items() if v is not None}


# --- invites ---------------------------------------------------------------------------------


@pytest.mark.parametrize("action", ["create_invite", "list_invites", "revoke_invite"])
async def test_invite_actions_need_an_invite_repository(pool_with_seed, make_user, action):
    admin = await make_user("noinv-admin", SUPERADMIN_ROLE_ID)
    bare = build_service(pool_with_seed, invites=False)
    args = {
        "create_invite": (admin.id, None, 24),
        "list_invites": (admin.id,),
        "revoke_invite": (admin.id, uuid4()),
    }[action]

    with pytest.raises(ForbiddenError, match="not configured"):
        await getattr(bare, action)(*args)


async def test_invite_mode_requires_repository_and_token(pool_with_seed):
    bare = build_service(pool_with_seed, invites=False, registration_mode="invite")
    with pytest.raises(ForbiddenError, match="not configured"):
        await bare.register("nobody", "nobody@example.com", PASSWORD)

    strict = build_service(pool_with_seed, registration_mode="invite")
    with pytest.raises(ForbiddenError, match="Invite token required"):
        await strict.register("nobody", "nobody@example.com", PASSWORD)


async def test_register_with_unknown_invite_is_rejected(service):
    with pytest.raises(InvalidTokenError):
        await service.register("invitee", "invitee@example.com", PASSWORD, invite_token=uuid4())


async def test_lost_invite_race_removes_the_new_user(pool_with_seed, make_user):
    admin = await make_user("race-admin", SUPERADMIN_ROLE_ID)
    invite = await build_service(pool_with_seed).create_invite(admin.id, None, 24)

    class LosingRepo(InviteRepository):
        async def claim_and_assign(self, token, user_id):
            return None

    racing = build_service(pool_with_seed, invite_repo=LosingRepo(pool_with_seed))
    with pytest.raises(InvalidTokenError):
        await racing.register("loser", "loser@example.com", PASSWORD, invite_token=invite.token)

    assert await UserRepository(pool_with_seed).get_by_email("loser@example.com") is None


async def test_failed_invite_claim_removes_the_new_user(pool_with_seed, make_user):
    admin = await make_user("fail-admin", SUPERADMIN_ROLE_ID)
    invite = await build_service(pool_with_seed).create_invite(admin.id, None, 24)

    class BrokenRepo(InviteRepository):
        async def claim_and_assign(self, token, user_id):
            raise RuntimeError("connection lost")

    broken = build_service(pool_with_seed, invite_repo=BrokenRepo(pool_with_seed))
    with pytest.raises(RuntimeError, match="connection lost"):
        await broken.register("victim", "victim@example.com", PASSWORD, invite_token=invite.token)

    assert await UserRepository(pool_with_seed).get_by_email("victim@example.com") is None


async def test_invite_is_single_use_and_revocable(service, make_user):
    admin = await make_user("single-admin", SUPERADMIN_ROLE_ID)
    invite = await service.create_invite(admin.id, "hint@example.com", 24)

    user, _ = await service.register(
        "invited", "invited@example.com", PASSWORD, invite_token=invite.token
    )
    assert user.username == "invited"
    with pytest.raises(InvalidTokenError):
        await service.register(
            "second", "second@example.com", PASSWORD, invite_token=invite.token
        )

    spare = await service.create_invite(admin.id, None, 24)
    assert await service.revoke_invite(admin.id, spare.token) is True
    assert [i.token for i in await service.list_invites(admin.id, active_only=True)] == []


async def test_invite_repository_claim_lifecycle(pool_with_seed, make_user):
    admin = await make_user("repo-admin", SUPERADMIN_ROLE_ID)
    user = await make_user("repo-user")
    repo = InviteRepository(pool_with_seed)
    expires = datetime.now(UTC) + timedelta(hours=1)

    invite = await repo.create(admin.id, None, expires)
    claimed = await repo.claim(invite.token)
    assert claimed is not None and claimed.used_at is not None
    assert await repo.claim(invite.token) is None

    marked = await repo.mark_used(invite.token, user.id)
    assert marked.used_by == user.id
    await repo.unclaim(invite.token)  # linked to a user: must stay consumed
    assert (await repo.get_by_token(invite.token)).used_at is not None

    other = await repo.create(admin.id, None, expires)
    await repo.claim(other.token)
    await repo.unclaim(other.token)
    assert (await repo.get_by_token(other.token)).used_at is None

    assert await repo.delete(other.token) is True
    assert await repo.delete(other.token) is False


# --- refresh / logout token validation -----------------------------------------------------


@pytest.mark.parametrize(
    "claims,message",
    [
        ({"type": "access"}, "Invalid token type"),
        ({"jti": None}, "missing jti"),
        ({"sub": None}, "missing user ID"),
    ],
)
@pytest.mark.parametrize("action", ["logout", "refresh_token"])
async def test_malformed_refresh_claims_are_rejected(service, action, claims, message):
    token = encode(refresh_claims(**claims))
    with pytest.raises(InvalidCredentialsError, match=message):
        await getattr(service, action)(token)


@pytest.mark.parametrize("action", ["logout", "refresh_token"])
async def test_garbage_refresh_token_is_rejected(service, action):
    with pytest.raises(InvalidCredentialsError):
        await getattr(service, action)("not-a-token")


async def test_refresh_for_deleted_user_fails(service):
    token = encode(refresh_claims())
    with pytest.raises(UserNotFoundError):
        await service.refresh_token(token)


async def test_refresh_and_logout_work_without_family_tracking(pool_with_seed, make_user):
    user = await make_user("nofam")
    plain = build_service(pool_with_seed, families=False)

    refreshed = await plain.refresh_token(create_refresh_token(str(user.id)))
    assert refreshed.access_token

    keep = create_refresh_token(str(user.id))
    await plain.logout(keep)
    with pytest.raises(InvalidCredentialsError, match="revoked"):
        await plain.refresh_token(keep)


async def test_reused_refresh_token_revokes_the_family(pool_with_seed, make_user, service):
    user = await make_user("reuse")
    families = TokenFamilyRepository(pool_with_seed)
    family_id = await families.create(user.id)
    token = create_refresh_token(str(user.id), family_id=str(family_id))

    await service.refresh_token(token)
    with pytest.raises(InvalidCredentialsError, match="revoked"):
        await service.refresh_token(token)
    assert await families.is_revoked(family_id) is True

    fresh = create_refresh_token(str(user.id), family_id=str(family_id))
    with pytest.raises(InvalidCredentialsError, match="family"):
        await service.refresh_token(fresh)


async def test_logout_revokes_the_whole_family(pool_with_seed, make_user, service):
    user = await make_user("famlogout")
    families = TokenFamilyRepository(pool_with_seed)
    family_id = await families.create(user.id)

    await service.logout(create_refresh_token(str(user.id), family_id=str(family_id)))
    assert await families.is_revoked(family_id) is True


async def test_revoke_all_user_families(pool_with_seed, make_user):
    user = await make_user("allfam")
    families = TokenFamilyRepository(pool_with_seed)
    first, second = await families.create(user.id), await families.create(user.id)

    await families.revoke_all_user_families(user.id)
    assert await families.is_revoked(first) and await families.is_revoked(second)
    assert await families.is_revoked(uuid4()) is False


async def test_access_token_lookup_failures(service, make_user):
    with pytest.raises(InvalidCredentialsError):
        await service.get_user_by_token("garbage")
    ghost = create_access_token(str(uuid4()))
    with pytest.raises(UserNotFoundError):
        await service.get_user_by_token(ghost)

    user = await make_user("sleeper")
    await service._user_repo.set_active(user.id, False)
    with pytest.raises(ForbiddenError, match="deactivated"):
        await service.get_user_by_token(create_access_token(str(user.id)))


# --- user management ----------------------------------------------------------------------------


async def test_update_user_flags_independently(service, make_user):
    admin = await make_user("upd-admin", SUPERADMIN_ROLE_ID)
    target = await make_user("upd-target")

    only_admin = await service.update_user(admin.id, target.id, None, True)
    assert only_admin.is_active is True
    assert "admin" in [n.lower() for n in await service._perm_svc.list_system_role_names(target.id)]

    deactivated = await service.update_user(admin.id, target.id, False, None)
    assert deactivated.is_active is False

    with pytest.raises(UserNotFoundError):
        await service.update_user(admin.id, uuid4(), True)


async def test_deactivating_a_superadmin_needs_another_one(service, make_user):
    first = await make_user("sa-one", SUPERADMIN_ROLE_ID)
    second = await make_user("sa-two", SUPERADMIN_ROLE_ID)
    manager = await make_user("sa-manager", ADMIN_ROLE_ID)

    deactivated = await service.update_user(manager.id, second.id, False)
    assert deactivated.is_active is False

    # The other superadmin is now inactive-but-present; once only one holder is left it is protected.
    await service._perm_svc.revoke_system_role(first.id, second.id, SUPERADMIN_ROLE_ID)
    with pytest.raises(ConflictError, match="last superadmin"):
        await service.update_user(manager.id, first.id, False)


async def test_delete_user_guards(service, make_user):
    first = await make_user("del-one", SUPERADMIN_ROLE_ID)
    second = await make_user("del-two", SUPERADMIN_ROLE_ID)

    with pytest.raises(NotFoundError):
        await service.delete_user(first.id, uuid4())
    with pytest.raises(ForbiddenError, match="own account"):
        await service.delete_user(first.id, first.id)

    assert await service.delete_user(first.id, second.id) is True


async def test_cannot_delete_the_last_superadmin(service, make_user):
    only = await make_user("only-sa", SUPERADMIN_ROLE_ID)
    deleter = await make_user("deleter", ADMIN_ROLE_ID)

    with pytest.raises(ConflictError, match="last superadmin"):
        await service.delete_user(deleter.id, only.id)


async def test_delete_reports_a_vanished_user(pool_with_seed, make_user):
    admin = await make_user("van-admin", SUPERADMIN_ROLE_ID)
    target = await make_user("van-target")

    class VanishingRepo(UserRepository):
        async def delete(self, user_id):
            return False

    service = build_service(pool_with_seed, user_repo=VanishingRepo(pool_with_seed))
    with pytest.raises(NotFoundError):
        await service.delete_user(admin.id, target.id)


async def test_admin_reset_generates_a_temporary_password(service, make_user):
    admin = await make_user("gen-admin", SUPERADMIN_ROLE_ID)
    target = await make_user("gen-target")

    user, password = await service.admin_reset_user(admin.id, target.id, None)
    assert password.startswith("Tmp1")
    assert user.password_change_required is True
    with pytest.raises(UserNotFoundError):
        await service.admin_reset_user(admin.id, uuid4(), None)


async def test_password_reset_flow_and_expiry(pool_with_seed, service, make_user):
    user = await make_user("resetme")
    token, expires_at = await service.request_password_reset(user.email)
    assert expires_at > datetime.now(UTC)

    tokens = await service.confirm_password_reset(token, "Brand-new-pass1")
    assert tokens.access_token
    with pytest.raises(InvalidCredentialsError, match="Invalid or expired"):
        await service.confirm_password_reset(token, "Brand-new-pass1")

    with pytest.raises(UserNotFoundError):
        await service.request_password_reset("nobody@example.com")

    expired = "expired-token"
    await PasswordResetRepository(pool_with_seed).create_token(
        expired, user.id, datetime.now(UTC) - timedelta(minutes=1)
    )
    with pytest.raises(InvalidCredentialsError, match="expired"):
        await service.confirm_password_reset(expired, "Brand-new-pass1")


# --- cleanup --------------------------------------------------------------------------------------


async def test_token_cleanup_removes_expired_tokens_and_dead_families(service_client, database_url):
    conn = await asyncpg.connect(database_url)
    try:
        user_id = await conn.fetchval(
            "INSERT INTO users (username, email, hashed_password, password_change_required, is_active) "
            "VALUES ('cleanup', 'cleanup@example.com', 'x', false, true) RETURNING id"
        )
        family_id = await conn.fetchval(
            "INSERT INTO refresh_token_families (user_id, revoked_at) VALUES ($1, now()) RETURNING id",
            user_id,
        )
        await conn.execute(
            "INSERT INTO revoked_tokens (jti, user_id, expires_at, family_id) "
            "VALUES ($1, $2, now() - interval '1 hour', $3)",
            uuid4(),
            user_id,
            family_id,
        )

        summary = await token_cleanup(datetime.now(UTC))
        assert summary == "revoked_tokens=1, families=1"
        assert await token_cleanup(datetime.now(UTC)) is None
    finally:
        await conn.close()


async def test_revoked_token_repository_cleanup(pool_with_seed, make_user):
    user = await make_user("revcleanup")
    repo = RevokedTokenRepository(pool_with_seed)
    jti = uuid4()
    await repo.revoke(jti, user.id, datetime.now(UTC) - timedelta(minutes=5))
    assert await repo.is_revoked(jti) is True

    assert await repo.cleanup_expired() == 1
    assert await repo.is_revoked(jti) is False
