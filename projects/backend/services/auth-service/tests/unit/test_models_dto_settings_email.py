"""Pure-logic tests: domain models, DTO helpers, settings validation and the email sender."""
from __future__ import annotations

import warnings
from datetime import UTC, datetime, timedelta
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch
from uuid import uuid4

import pytest

from auth_service.domain import dto
from auth_service.domain.dto import (
    PROJECT_ROLE_NAME_TO_ID,
    ProjectMemberUpdateRequest,
    UserRegisterRequest,
)
from auth_service.domain.models import (
    Project,
    RolePermission,
    User,
    UserProjectRole,
    UserSystemRole,
)
from auth_service.services.email import EmailService
from auth_service.settings import Settings

NOW = datetime.now(UTC)


def make_user() -> User:
    return User(
        id=uuid4(),
        username="alice",
        email="alice@example.com",
        hashed_password="hash",
        password_change_required=True,
        is_active=True,
        created_at=NOW,
        updated_at=NOW,
    )


def test_user_to_dict_hides_the_password_hash_by_default():
    user = make_user()
    public = user.to_dict()
    private = user.to_dict(exclude_password=False)

    assert "hashed_password" not in public
    assert public["password_change_required"] is True
    assert private["hashed_password"] == "hash"


def test_project_to_dict_round_trips_through_a_row():
    row = {
        "id": uuid4(),
        "name": "Wind tunnel",
        "description": None,
        "owner_id": uuid4(),
        "created_at": NOW,
        "updated_at": NOW,
    }
    data = Project.from_row(row).to_dict()

    assert data["name"] == "Wind tunnel"
    assert data["description"] is None
    assert data["owner_id"] == str(row["owner_id"])


def test_role_permission_from_row():
    role_id = uuid4()
    pair = RolePermission.from_row({"role_id": role_id, "permission_id": "users.list"})
    assert (pair.role_id, pair.permission_id) == (role_id, "users.list")


@pytest.mark.parametrize("model", [UserSystemRole, UserProjectRole])
@pytest.mark.parametrize(
    "offset,expired",
    [(None, False), (timedelta(hours=-1), True), (timedelta(hours=1), False)],
)
def test_role_assignment_expiry(model, offset, expired):
    row = {
        "user_id": uuid4(),
        "project_id": uuid4(),
        "role_id": uuid4(),
        "granted_by": uuid4(),
        "granted_at": NOW,
        "expires_at": None if offset is None else NOW + offset,
    }
    assert model.from_row(row).is_expired is expired


def test_password_complexity_is_enforced_when_enabled(monkeypatch):
    monkeypatch.setattr(dto, "PASSWORD_COMPLEXITY_ENABLED", True)

    with pytest.raises(ValueError, match="uppercase"):
        UserRegisterRequest(username="alice", email="alice@example.com", password="alllowercase1")
    ok = UserRegisterRequest(username="alice", email="alice@example.com", password="Passw0rd-ok")
    assert ok.password == "Passw0rd-ok"


@pytest.mark.parametrize(
    "kwargs,expected",
    [
        ({"role_id": "explicit"}, "explicit"),
        ({"role": "Editor"}, PROJECT_ROLE_NAME_TO_ID["editor"]),
    ],
)
def test_member_update_resolves_role(kwargs, expected):
    assert ProjectMemberUpdateRequest(**kwargs).resolved_role_id() == expected


def test_member_update_rejects_unknown_or_missing_role():
    with pytest.raises(ValueError, match="Unknown role name"):
        ProjectMemberUpdateRequest(role="emperor").resolved_role_id()
    with pytest.raises(ValueError, match="Either role_id or role"):
        ProjectMemberUpdateRequest().resolved_role_id()


@pytest.mark.parametrize(
    "secret,fragment",
    [
        ("secret", "known insecure default"),
        ("short-but-unique", "shorter than 32"),
    ],
)
def test_weak_jwt_secrets_are_flagged(secret, fragment):
    with pytest.warns(UserWarning, match=fragment):
        Settings(jwt_secret=secret)


def test_strong_jwt_secret_is_silent():
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        Settings(jwt_secret="x" * 40)


def email_settings(**overrides):
    values = {
        "smtp_enabled": True,
        "smtp_host": "mail.local",
        "smtp_port": 2525,
        "smtp_user": "",
        "smtp_password": "",
        "smtp_from": "noreply@example.com",
        "smtp_from_name": "Platform",
        "app_url": "https://app.example.com",
    }
    values.update(overrides)
    return SimpleNamespace(**values)


async def send(settings, **patch_kwargs):
    service = EmailService(settings)  # type: ignore[arg-type]
    with patch("auth_service.services.email.aiosmtplib.send", AsyncMock(**patch_kwargs)) as sender:
        await service.send_password_reset_email("bob@example.com", "tok123", NOW)
    return sender


async def test_email_is_skipped_when_smtp_is_disabled():
    sender = await send(email_settings(smtp_enabled=False))
    sender.assert_not_called()


async def test_email_is_built_with_a_reset_link():
    sender = await send(email_settings())

    sender.assert_awaited_once()
    kwargs = sender.await_args.kwargs
    message = kwargs["message"]
    assert message["To"] == "bob@example.com"
    assert "Platform" in message["From"]
    body = "".join(part.get_payload(decode=True).decode() for part in message.get_payload())
    assert "https://app.example.com/reset-password?token=tok123" in body
    assert (kwargs["hostname"], kwargs["port"]) == ("mail.local", 2525)
    assert "username" not in kwargs


async def test_email_uses_credentials_when_configured():
    sender = await send(email_settings(smtp_user="mailer", smtp_password="pw"))

    kwargs = sender.await_args.kwargs
    assert (kwargs["username"], kwargs["password"]) == ("mailer", "pw")


async def test_email_failures_are_swallowed():
    sender = await send(email_settings(), side_effect=OSError("connection refused"))
    sender.assert_awaited_once()
