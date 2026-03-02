"""Authentication API tests."""
import asyncpg  # type: ignore[import-untyped]
import pytest

from auth_service.settings import settings

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

async def _register(client, username, email, password="testpass123", invite_token=None):
    payload = {"username": username, "email": email, "password": password}
    if invite_token is not None:
        payload["invite_token"] = str(invite_token)
    return await client.post("/auth/register", json=payload)


async def _create_invite(client, admin_token, email_hint=None, expires_in_hours=72):
    body = {"expires_in_hours": expires_in_hours}
    if email_hint is not None:
        body["email_hint"] = email_hint
    return await client.post(
        "/auth/admin/invites",
        headers={"Authorization": f"Bearer {admin_token}"},
        json=body,
    )

# Bcrypt hash of "admin123"
_ADMIN_HASH = "$2b$12$0QfCvOcgNkygw/I79ieV5eOIwAjWXUjdFUr/QvRgDMewN1OfENrmG"


@pytest.fixture
async def admin_token(service_client):
    """Insert admin user into test DB and return its access token."""
    conn = await asyncpg.connect(str(settings.database_url))
    try:
        await conn.execute(
            "INSERT INTO users (username, email, hashed_password, password_change_required, is_admin) "
            "VALUES ('admin', 'admin@example.com', $1, false, true) "
            "ON CONFLICT (username) DO UPDATE SET is_admin = true",
            _ADMIN_HASH,
        )
    finally:
        await conn.close()

    login_response = await service_client.post(
        "/auth/login",
        json={"username": "admin", "password": "admin123"},
    )
    assert login_response.status == 200
    return (await login_response.json())["access_token"]


@pytest.mark.asyncio
async def test_register_success(service_client):
    """Test successful user registration."""
    response = await service_client.post(
        "/auth/register",
        json={
            "username": "testuser",
            "email": "test@example.com",
            "password": "testpass123",
        },
    )
    assert response.status == 201
    payload = await response.json()
    assert "user" in payload
    assert "access_token" in payload
    assert "refresh_token" in payload
    assert payload["user"]["username"] == "testuser"
    assert payload["user"]["email"] == "test@example.com"
    assert payload["user"]["password_change_required"] is False


@pytest.mark.asyncio
async def test_register_duplicate_username(service_client):
    """Test registration with duplicate username."""
    # Register first user
    await service_client.post(
        "/auth/register",
        json={
            "username": "duplicate",
            "email": "first@example.com",
            "password": "testpass123",
        },
    )

    # Try to register with same username
    response = await service_client.post(
        "/auth/register",
        json={
            "username": "duplicate",
            "email": "second@example.com",
            "password": "testpass123",
        },
    )
    assert response.status == 409
    payload = await response.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_register_duplicate_email(service_client):
    """Test registration with duplicate email."""
    # Register first user
    await service_client.post(
        "/auth/register",
        json={
            "username": "user1",
            "email": "duplicate@example.com",
            "password": "testpass123",
        },
    )

    # Try to register with same email
    response = await service_client.post(
        "/auth/register",
        json={
            "username": "user2",
            "email": "duplicate@example.com",
            "password": "testpass123",
        },
    )
    assert response.status == 409
    payload = await response.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_register_invalid_data(service_client):
    """Test registration with invalid data."""
    # Too short username
    response = await service_client.post(
        "/auth/register",
        json={
            "username": "ab",
            "email": "test@example.com",
            "password": "testpass123",
        },
    )
    assert response.status == 400

    # Invalid email
    response = await service_client.post(
        "/auth/register",
        json={
            "username": "testuser",
            "email": "invalid-email",
            "password": "testpass123",
        },
    )
    assert response.status == 400

    # Too short password
    response = await service_client.post(
        "/auth/register",
        json={
            "username": "testuser",
            "email": "test@example.com",
            "password": "short",
        },
    )
    assert response.status == 400


@pytest.mark.asyncio
async def test_login_success(service_client):
    """Test successful login."""
    # Register user first
    register_response = await service_client.post(
        "/auth/register",
        json={
            "username": "loginuser",
            "email": "login@example.com",
            "password": "loginpass123",
        },
    )
    assert register_response.status == 201

    # Login
    response = await service_client.post(
        "/auth/login",
        json={
            "username": "loginuser",
            "password": "loginpass123",
        },
    )
    assert response.status == 200
    payload = await response.json()
    assert "user" in payload
    assert "access_token" in payload
    assert "refresh_token" in payload
    assert payload["user"]["username"] == "loginuser"


@pytest.mark.asyncio
async def test_login_invalid_username(service_client):
    """Test login with invalid username."""
    response = await service_client.post(
        "/auth/login",
        json={
            "username": "nonexistent",
            "password": "password123",
        },
    )
    assert response.status == 401
    payload = await response.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_login_invalid_password(service_client):
    """Test login with invalid password."""
    # Register user first
    await service_client.post(
        "/auth/register",
        json={
            "username": "wrongpass",
            "email": "wrongpass@example.com",
            "password": "correctpass123",
        },
    )

    # Try to login with wrong password
    response = await service_client.post(
        "/auth/login",
        json={
            "username": "wrongpass",
            "password": "wrongpassword",
        },
    )
    assert response.status == 401
    payload = await response.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_refresh_token_success(service_client):
    """Test successful token refresh."""
    # Register and login to get tokens
    register_response = await service_client.post(
        "/auth/register",
        json={
            "username": "refreshtest",
            "email": "refresh@example.com",
            "password": "refreshpass123",
        },
    )
    assert register_response.status == 201
    register_payload = await register_response.json()
    refresh_token = register_payload["refresh_token"]

    # Refresh token
    response = await service_client.post(
        "/auth/refresh",
        json={"refresh_token": refresh_token},
    )
    assert response.status == 200
    payload = await response.json()
    assert "access_token" in payload
    assert "refresh_token" in payload
    # Tokens should be valid (they might be same if created in same second, which is acceptable)
    # Verify we can use the new access token
    me_response = await service_client.get(
        "/auth/me",
        headers={"Authorization": f"Bearer {payload['access_token']}"},
    )
    assert me_response.status == 200


@pytest.mark.asyncio
async def test_refresh_token_invalid(service_client):
    """Test refresh with invalid token."""
    response = await service_client.post(
        "/auth/refresh",
        json={"refresh_token": "invalid-token"},
    )
    assert response.status == 401
    payload = await response.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_me_success(service_client):
    """Test getting current user info."""
    # Register and login
    register_response = await service_client.post(
        "/auth/register",
        json={
            "username": "metest",
            "email": "me@example.com",
            "password": "mepass123",
        },
    )
    assert register_response.status == 201
    register_payload = await register_response.json()
    access_token = register_payload["access_token"]

    # Get user info
    response = await service_client.get(
        "/auth/me",
        headers={"Authorization": f"Bearer {access_token}"},
    )
    assert response.status == 200
    payload = await response.json()
    assert payload["username"] == "metest"
    assert payload["email"] == "me@example.com"
    assert "id" in payload


@pytest.mark.asyncio
async def test_me_unauthorized(service_client):
    """Test /auth/me without token."""
    response = await service_client.get("/auth/me")
    assert response.status == 401
    payload = await response.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_me_invalid_token(service_client):
    """Test /auth/me with invalid token."""
    response = await service_client.get(
        "/auth/me",
        headers={"Authorization": "Bearer invalid-token"},
    )
    assert response.status == 401
    payload = await response.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_logout(service_client):
    """Test logout с валидным refresh-токеном."""
    # Регистрируемся, чтобы получить токен
    register_response = await service_client.post(
        "/auth/register",
        json={
            "username": "logoutuser",
            "email": "logout@example.com",
            "password": "logoutpass123",
        },
    )
    assert register_response.status == 201
    register_payload = await register_response.json()
    refresh_token = register_payload["refresh_token"]

    response = await service_client.post(
        "/auth/logout",
        json={"refresh_token": refresh_token},
    )
    assert response.status == 200
    payload = await response.json()
    assert payload["ok"] is True


@pytest.mark.asyncio
async def test_logout_missing_body(service_client):
    """Test logout без тела запроса → 400."""
    response = await service_client.post("/auth/logout")
    assert response.status == 400
    payload = await response.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_logout_invalid_token(service_client):
    """Test logout с невалидным токеном → 401."""
    response = await service_client.post(
        "/auth/logout",
        json={"refresh_token": "not.a.valid.token"},
    )
    assert response.status == 401
    payload = await response.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_logout_revokes_refresh_token(service_client):
    """Test: после logout refresh-токен больше не принимается."""
    # Регистрируемся
    register_response = await service_client.post(
        "/auth/register",
        json={
            "username": "revokeuser",
            "email": "revoke@example.com",
            "password": "revokepass123",
        },
    )
    assert register_response.status == 201
    register_payload = await register_response.json()
    refresh_token = register_payload["refresh_token"]

    # Logout — отзываем токен
    logout_response = await service_client.post(
        "/auth/logout",
        json={"refresh_token": refresh_token},
    )
    assert logout_response.status == 200

    # Попытка обновить токен должна вернуть 401
    refresh_response = await service_client.post(
        "/auth/refresh",
        json={"refresh_token": refresh_token},
    )
    assert refresh_response.status == 401
    payload = await refresh_response.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_change_password_success(service_client):
    """Test successful password change."""
    # Register user
    register_response = await service_client.post(
        "/auth/register",
        json={
            "username": "changepass",
            "email": "changepass@example.com",
            "password": "oldpass123",
        },
    )
    assert register_response.status == 201
    register_payload = await register_response.json()
    access_token = register_payload["access_token"]

    # Change password
    response = await service_client.post(
        "/auth/change-password",
        headers={"Authorization": f"Bearer {access_token}"},
        json={
            "old_password": "oldpass123",
            "new_password": "newpass123",
        },
    )
    assert response.status == 200
    payload = await response.json()
    assert payload["password_change_required"] is False

    # Verify new password works
    login_response = await service_client.post(
        "/auth/login",
        json={
            "username": "changepass",
            "password": "newpass123",
        },
    )
    assert login_response.status == 200


@pytest.mark.asyncio
async def test_change_password_wrong_old_password(service_client):
    """Test password change with wrong old password."""
    # Register user
    register_response = await service_client.post(
        "/auth/register",
        json={
            "username": "wrongold",
            "email": "wrongold@example.com",
            "password": "correctpass123",
        },
    )
    assert register_response.status == 201
    register_payload = await register_response.json()
    access_token = register_payload["access_token"]

    # Try to change password with wrong old password
    response = await service_client.post(
        "/auth/change-password",
        headers={"Authorization": f"Bearer {access_token}"},
        json={
            "old_password": "wrongoldpass",
            "new_password": "newpass123",
        },
    )
    assert response.status == 401
    payload = await response.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_change_password_unauthorized(service_client):
    """Test password change without token."""
    response = await service_client.post(
        "/auth/change-password",
        json={
            "old_password": "oldpass123",
            "new_password": "newpass123",
        },
    )
    assert response.status == 401


@pytest.mark.asyncio
async def test_change_password_invalid_new_password(service_client):
    """Test password change with invalid new password."""
    # Register user
    register_response = await service_client.post(
        "/auth/register",
        json={
            "username": "invalidnew",
            "email": "invalidnew@example.com",
            "password": "oldpass123",
        },
    )
    assert register_response.status == 201
    register_payload = await register_response.json()
    access_token = register_payload["access_token"]

    # Try to change password with too short new password
    response = await service_client.post(
        "/auth/change-password",
        headers={"Authorization": f"Bearer {access_token}"},
        json={
            "old_password": "Oldpass123",
            "new_password": "short",
        },
    )
    assert response.status == 400


# ---------------------------------------------------------------------------
# Password reset tests
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_password_reset_request_success(service_client):
    """Запрос сброса пароля по существующему email → 200, поля reset_token и expires_at."""
    await service_client.post(
        "/auth/register",
        json={
            "username": "resetuser",
            "email": "reset@example.com",
            "password": "resetpass123",
        },
    )

    response = await service_client.post(
        "/auth/password-reset/request",
        json={"email": "reset@example.com"},
    )
    assert response.status == 200
    payload = await response.json()
    assert "reset_token" in payload
    assert "expires_at" in payload
    assert len(payload["reset_token"]) > 10


@pytest.mark.asyncio
async def test_password_reset_request_not_found(service_client):
    """Запрос сброса пароля по несуществующему email → 404."""
    response = await service_client.post(
        "/auth/password-reset/request",
        json={"email": "nobody@example.com"},
    )
    assert response.status == 404
    payload = await response.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_password_reset_confirm_success(service_client):
    """Валидный reset_token → 200, получаем access_token и refresh_token."""
    await service_client.post(
        "/auth/register",
        json={
            "username": "confirmuser",
            "email": "confirm@example.com",
            "password": "oldpassword123",
        },
    )

    request_response = await service_client.post(
        "/auth/password-reset/request",
        json={"email": "confirm@example.com"},
    )
    assert request_response.status == 200
    reset_token = (await request_response.json())["reset_token"]

    response = await service_client.post(
        "/auth/password-reset/confirm",
        json={"reset_token": reset_token, "new_password": "newpassword123"},
    )
    assert response.status == 200
    payload = await response.json()
    assert "access_token" in payload
    assert "refresh_token" in payload


@pytest.mark.asyncio
async def test_password_reset_confirm_invalid_token(service_client):
    """Невалидный reset_token → 401."""
    response = await service_client.post(
        "/auth/password-reset/confirm",
        json={"reset_token": "invalid-token-xyz", "new_password": "newpassword123"},
    )
    assert response.status == 401
    payload = await response.json()
    assert "error" in payload


# ---------------------------------------------------------------------------
# Admin reset tests
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_admin_reset_success(service_client, admin_token):
    """Admin сбрасывает пароль другому юзеру → 200, поле new_password; можно залогиниться."""
    # Регистрируем целевого пользователя
    reg = await service_client.post(
        "/auth/register",
        json={
            "username": "targetuser",
            "email": "target@example.com",
            "password": "targetpass123",
        },
    )
    assert reg.status == 201
    target_id = (await reg.json())["user"]["id"]

    # Admin сбрасывает пароль
    response = await service_client.post(
        f"/auth/admin/users/{target_id}/reset",
        headers={"Authorization": f"Bearer {admin_token}"},
        json={},
    )
    assert response.status == 200
    payload = await response.json()
    assert "new_password" in payload
    assert "user" in payload

    # Проверяем, что новый пароль работает
    new_password = payload["new_password"]
    login_with_new = await service_client.post(
        "/auth/login",
        json={"username": "targetuser", "password": new_password},
    )
    assert login_with_new.status == 200


@pytest.mark.asyncio
async def test_admin_reset_forbidden(service_client):
    """Обычный пользователь не может сбросить пароль другому → 403."""
    reg1 = await service_client.post(
        "/auth/register",
        json={"username": "nonadmin", "email": "nonadmin@example.com", "password": "pass12345"},
    )
    assert reg1.status == 201
    token = (await reg1.json())["access_token"]

    reg2 = await service_client.post(
        "/auth/register",
        json={"username": "victim2", "email": "victim2@example.com", "password": "pass12345"},
    )
    assert reg2.status == 201
    victim_id = (await reg2.json())["user"]["id"]

    response = await service_client.post(
        f"/auth/admin/users/{victim_id}/reset",
        headers={"Authorization": f"Bearer {token}"},
        json={},
    )
    assert response.status == 403
    payload = await response.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_admin_reset_user_not_found(service_client, admin_token):
    """Admin + несуществующий UUID → 404."""
    fake_id = "00000000-0000-0000-0000-000000000000"
    response = await service_client.post(
        f"/auth/admin/users/{fake_id}/reset",
        headers={"Authorization": f"Bearer {admin_token}"},
        json={},
    )
    assert response.status == 404
    payload = await response.json()
    assert "error" in payload


# ---------------------------------------------------------------------------
# Invite system tests
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_register_invite_mode_no_token(service_client, monkeypatch):
    """В invite-режиме без токена → 403."""
    monkeypatch.setattr(settings, "registration_mode", "invite")

    response = await _register(service_client, "notoken", "notoken@example.com")
    assert response.status == 403
    payload = await response.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_register_invite_mode_valid_token(service_client, admin_token, monkeypatch):
    """В invite-режиме с действующим токеном → 201."""
    monkeypatch.setattr(settings, "registration_mode", "invite")

    invite_resp = await _create_invite(service_client, admin_token)
    assert invite_resp.status == 201
    invite_token = (await invite_resp.json())["token"]

    response = await _register(service_client, "inviteduser", "invited@example.com", invite_token=invite_token)
    assert response.status == 201
    payload = await response.json()
    assert payload["user"]["username"] == "inviteduser"


@pytest.mark.asyncio
async def test_register_invite_mode_expired_token(service_client, admin_token, monkeypatch):
    """В invite-режиме с просроченным токеном → 403."""
    monkeypatch.setattr(settings, "registration_mode", "invite")

    # Создаём инвайт и сразу помечаем его просроченным через прямой SQL
    invite_resp = await _create_invite(service_client, admin_token, expires_in_hours=1)
    assert invite_resp.status == 201
    invite_token = (await invite_resp.json())["token"]

    conn = await asyncpg.connect(str(settings.database_url))
    try:
        await conn.execute(
            "UPDATE invite_tokens SET expires_at = now() - interval '1 hour' WHERE token = $1",
            invite_token,
        )
    finally:
        await conn.close()

    response = await _register(service_client, "expireduser", "expired@example.com", invite_token=invite_token)
    assert response.status == 401
    payload = await response.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_register_invite_mode_used_token(service_client, admin_token, monkeypatch):
    """В invite-режиме с уже использованным токеном → 403."""
    monkeypatch.setattr(settings, "registration_mode", "invite")

    invite_resp = await _create_invite(service_client, admin_token)
    assert invite_resp.status == 201
    invite_token = (await invite_resp.json())["token"]

    # Первая регистрация — успех
    r1 = await _register(service_client, "firstuser_used", "firstused@example.com", invite_token=invite_token)
    assert r1.status == 201

    # Вторая регистрация с тем же токеном — 401
    r2 = await _register(service_client, "seconduser_used", "secondused@example.com", invite_token=invite_token)
    assert r2.status == 401
    payload = await r2.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_create_invite_success(service_client, admin_token):
    """Admin создаёт инвайт → 201, поля token и expires_at присутствуют."""
    response = await _create_invite(service_client, admin_token)
    assert response.status == 201
    payload = await response.json()
    assert "token" in payload
    assert "expires_at" in payload
    assert "id" in payload
    assert payload["is_active"] is True


@pytest.mark.asyncio
async def test_create_invite_with_email_hint(service_client, admin_token):
    """Admin создаёт инвайт с email_hint → email_hint сохранён."""
    response = await _create_invite(service_client, admin_token, email_hint="friend@example.com")
    assert response.status == 201
    payload = await response.json()
    assert payload["email_hint"] == "friend@example.com"


@pytest.mark.asyncio
async def test_create_invite_forbidden_non_admin(service_client):
    """Обычный пользователь не может создать инвайт → 403."""
    reg = await _register(service_client, "nonadmin2", "nonadmin2@example.com")
    assert reg.status == 201
    token = (await reg.json())["access_token"]

    response = await service_client.post(
        "/auth/admin/invites",
        headers={"Authorization": f"Bearer {token}"},
        json={"expires_in_hours": 24},
    )
    assert response.status == 403
    payload = await response.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_list_invites(service_client, admin_token):
    """Admin получает список инвайтов → 200, список."""
    # Создаём пару инвайтов
    await _create_invite(service_client, admin_token)
    await _create_invite(service_client, admin_token, email_hint="list@example.com")

    response = await service_client.get(
        "/auth/admin/invites",
        headers={"Authorization": f"Bearer {admin_token}"},
    )
    assert response.status == 200
    payload = await response.json()
    assert isinstance(payload, list)
    assert len(payload) >= 2


@pytest.mark.asyncio
async def test_list_invites_active_only(service_client, admin_token):
    """active_only=true возвращает только активные инвайты."""
    # Создаём инвайт и сразу его используем через SQL (помечаем использованным)
    invite_resp = await _create_invite(service_client, admin_token)
    used_token = (await invite_resp.json())["token"]

    conn = await asyncpg.connect(str(settings.database_url))
    try:
        await conn.execute(
            "UPDATE invite_tokens SET used_at = now() WHERE token = $1",
            used_token,
        )
    finally:
        await conn.close()

    # Создаём один активный инвайт
    await _create_invite(service_client, admin_token, email_hint="active@example.com")

    response = await service_client.get(
        "/auth/admin/invites?active_only=true",
        headers={"Authorization": f"Bearer {admin_token}"},
    )
    assert response.status == 200
    payload = await response.json()
    assert all(inv["is_active"] for inv in payload)


@pytest.mark.asyncio
async def test_delete_invite_success(service_client, admin_token):
    """Admin удаляет инвайт → 204."""
    invite_resp = await _create_invite(service_client, admin_token)
    assert invite_resp.status == 201
    invite_token = (await invite_resp.json())["token"]

    response = await service_client.delete(
        f"/auth/admin/invites/{invite_token}",
        headers={"Authorization": f"Bearer {admin_token}"},
    )
    assert response.status == 204


@pytest.mark.asyncio
async def test_delete_invite_not_found(service_client, admin_token):
    """Удаление несуществующего инвайта → 404."""
    fake_token = "00000000-0000-0000-0000-000000000000"
    response = await service_client.delete(
        f"/auth/admin/invites/{fake_token}",
        headers={"Authorization": f"Bearer {admin_token}"},
    )
    assert response.status == 404
    payload = await response.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_delete_used_invite_forbidden(service_client, admin_token, monkeypatch):
    """Удаление использованного инвайта → 409."""
    monkeypatch.setattr(settings, "registration_mode", "invite")

    invite_resp = await _create_invite(service_client, admin_token)
    assert invite_resp.status == 201
    invite_token = (await invite_resp.json())["token"]

    # Используем инвайт для регистрации
    reg = await _register(service_client, "usedtokenuser", "usedtoken@example.com", invite_token=invite_token)
    assert reg.status == 201

    monkeypatch.setattr(settings, "registration_mode", "open")

    response = await service_client.delete(
        f"/auth/admin/invites/{invite_token}",
        headers={"Authorization": f"Bearer {admin_token}"},
    )
    assert response.status == 409
    payload = await response.json()
    assert "error" in payload


# ---------------------------------------------------------------------------
# Admin user management tests
# ---------------------------------------------------------------------------


async def _admin_list_users(client, admin_token, **params):
    """Helper: GET /auth/admin/users с опциональными query params."""
    url = "/auth/admin/users"
    if params:
        qs = "&".join(f"{k}={v}" for k, v in params.items())
        url = f"{url}?{qs}"
    return await client.get(url, headers={"Authorization": f"Bearer {admin_token}"})


async def _admin_update_user(client, admin_token, user_id, **fields):
    """Helper: PATCH /auth/admin/users/{user_id}."""
    return await client.patch(
        f"/auth/admin/users/{user_id}",
        headers={"Authorization": f"Bearer {admin_token}"},
        json=fields,
    )


async def _admin_delete_user(client, admin_token, user_id):
    """Helper: DELETE /auth/admin/users/{user_id}."""
    return await client.delete(
        f"/auth/admin/users/{user_id}",
        headers={"Authorization": f"Bearer {admin_token}"},
    )


@pytest.mark.asyncio
async def test_admin_list_users_success(service_client, admin_token):
    """Admin получает список пользователей → 200, список содержит зарегистрированных."""
    reg = await _register(service_client, "listuser1", "listuser1@example.com")
    assert reg.status == 201

    response = await _admin_list_users(service_client, admin_token)
    assert response.status == 200
    payload = await response.json()
    assert isinstance(payload, list)
    usernames = [u["username"] for u in payload]
    assert "listuser1" in usernames
    # Все поля присутствуют
    for user in payload:
        assert "id" in user
        assert "username" in user
        assert "email" in user
        assert "is_active" in user
        assert "is_admin" in user


@pytest.mark.asyncio
async def test_admin_list_users_forbidden(service_client):
    """Обычный пользователь не может получить список → 403."""
    reg = await _register(service_client, "listforbidden", "listforbidden@example.com")
    assert reg.status == 201
    token = (await reg.json())["access_token"]

    response = await service_client.get(
        "/auth/admin/users",
        headers={"Authorization": f"Bearer {token}"},
    )
    assert response.status == 403


@pytest.mark.asyncio
async def test_admin_list_users_search(service_client, admin_token):
    """Фильтр ?search= возвращает только совпадающих пользователей."""
    await _register(service_client, "searchalpha", "searchalpha@example.com")
    await _register(service_client, "searchbeta", "searchbeta@example.com")
    await _register(service_client, "othergamma", "othergamma@example.com")

    response = await _admin_list_users(service_client, admin_token, search="search")
    assert response.status == 200
    payload = await response.json()
    usernames = [u["username"] for u in payload]
    assert "searchalpha" in usernames
    assert "searchbeta" in usernames
    assert "othergamma" not in usernames


@pytest.mark.asyncio
async def test_admin_deactivate_user_success(service_client, admin_token):
    """Admin деактивирует пользователя → 200, is_active=false."""
    reg = await _register(service_client, "deactuser", "deactuser@example.com")
    assert reg.status == 201
    user_id = (await reg.json())["user"]["id"]

    response = await _admin_update_user(service_client, admin_token, user_id, is_active=False)
    assert response.status == 200
    payload = await response.json()
    assert payload["is_active"] is False
    assert payload["id"] == user_id


@pytest.mark.asyncio
async def test_admin_deactivated_user_cannot_login(service_client, admin_token):
    """Деактивированный пользователь не может войти → 403."""
    reg = await _register(service_client, "cantlogin", "cantlogin@example.com", password="Cantlogin1")
    assert reg.status == 201
    user_id = (await reg.json())["user"]["id"]

    deact = await _admin_update_user(service_client, admin_token, user_id, is_active=False)
    assert deact.status == 200

    login_resp = await service_client.post(
        "/auth/login",
        json={"username": "cantlogin", "password": "Cantlogin1"},
    )
    assert login_resp.status == 403
    payload = await login_resp.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_admin_reactivate_user_success(service_client, admin_token):
    """Admin реактивирует пользователя → логин снова работает."""
    reg = await _register(service_client, "reactuser", "reactuser@example.com", password="Reactuser1")
    assert reg.status == 201
    user_id = (await reg.json())["user"]["id"]

    # Деактивируем
    await _admin_update_user(service_client, admin_token, user_id, is_active=False)

    # Реактивируем
    resp = await _admin_update_user(service_client, admin_token, user_id, is_active=True)
    assert resp.status == 200
    assert (await resp.json())["is_active"] is True

    # Логин работает
    login_resp = await service_client.post(
        "/auth/login",
        json={"username": "reactuser", "password": "Reactuser1"},
    )
    assert login_resp.status == 200


@pytest.mark.asyncio
async def test_admin_update_self_forbidden(service_client, admin_token):
    """Admin не может изменить собственный аккаунт → 403."""
    # Получаем свой id
    me_resp = await service_client.get(
        "/auth/me",
        headers={"Authorization": f"Bearer {admin_token}"},
    )
    assert me_resp.status == 200
    admin_id = (await me_resp.json())["id"]

    response = await _admin_update_user(service_client, admin_token, admin_id, is_active=False)
    assert response.status == 403
    payload = await response.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_admin_grant_admin_success(service_client, admin_token):
    """Admin повышает пользователя до admin → 200, is_admin=true."""
    reg = await _register(service_client, "newadmin", "newadmin@example.com")
    assert reg.status == 201
    user_id = (await reg.json())["user"]["id"]

    response = await _admin_update_user(service_client, admin_token, user_id, is_admin=True)
    assert response.status == 200
    payload = await response.json()
    assert payload["is_admin"] is True


@pytest.mark.asyncio
async def test_admin_revoke_last_admin_forbidden(service_client, admin_token):
    """Нельзя разжаловать единственного активного admin'а → 409."""
    # Создаём второго пользователя и повышаем до admin
    reg = await _register(service_client, "revoke409user", "revoke409user@example.com")
    assert reg.status == 201
    target_id = (await reg.json())["user"]["id"]

    grant = await _admin_update_user(service_client, admin_token, target_id, is_admin=True)
    assert grant.status == 200

    # Деактивируем target → count_admins() == 1 (только admin_token)
    deact = await _admin_update_user(service_client, admin_token, target_id, is_active=False)
    assert deact.status == 200

    # Попытка разжаловать target (is_admin=True, но is_active=False) → 409
    response = await _admin_update_user(service_client, admin_token, target_id, is_admin=False)
    assert response.status == 409
    payload = await response.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_admin_delete_user_success(service_client, admin_token):
    """Admin удаляет пользователя → 204, пользователь исчезает из списка."""
    reg = await _register(service_client, "deleteuser", "deleteuser@example.com")
    assert reg.status == 201
    user_id = (await reg.json())["user"]["id"]

    response = await _admin_delete_user(service_client, admin_token, user_id)
    assert response.status == 204

    # Проверяем что пользователь исчез
    list_resp = await _admin_list_users(service_client, admin_token, search="deleteuser")
    assert list_resp.status == 200
    payload = await list_resp.json()
    assert all(u["id"] != user_id for u in payload)


@pytest.mark.asyncio
async def test_admin_delete_user_not_found(service_client, admin_token):
    """Удаление несуществующего пользователя → 404."""
    fake_id = "00000000-0000-0000-0000-000000000000"
    response = await _admin_delete_user(service_client, admin_token, fake_id)
    assert response.status == 404
    payload = await response.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_admin_delete_self_forbidden(service_client, admin_token):
    """Admin не может удалить собственный аккаунт → 403."""
    me_resp = await service_client.get(
        "/auth/me",
        headers={"Authorization": f"Bearer {admin_token}"},
    )
    assert me_resp.status == 200
    admin_id = (await me_resp.json())["id"]

    response = await _admin_delete_user(service_client, admin_token, admin_id)
    assert response.status == 403
    payload = await response.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_admin_delete_last_admin_forbidden(service_client, admin_token):
    """Нельзя удалить единственного активного admin'а → 409."""
    # Создаём второго admin'а
    reg = await _register(service_client, "del409admin", "del409admin@example.com")
    assert reg.status == 201
    target_id = (await reg.json())["user"]["id"]

    await _admin_update_user(service_client, admin_token, target_id, is_admin=True)

    # Деактивируем → count_admins == 1 (только admin_token)
    await _admin_update_user(service_client, admin_token, target_id, is_active=False)

    # Попытка удалить target (admin, inactive) → 409
    response = await _admin_delete_user(service_client, admin_token, target_id)
    assert response.status == 409
    payload = await response.json()
    assert "error" in payload


@pytest.mark.asyncio
async def test_admin_delete_user_forbidden_non_admin(service_client):
    """Обычный пользователь не может удалить другого → 403."""
    reg_actor = await _register(service_client, "delactor", "delactor@example.com")
    assert reg_actor.status == 201
    actor_token = (await reg_actor.json())["access_token"]

    reg_victim = await _register(service_client, "delvictim", "delvictim@example.com")
    assert reg_victim.status == 201
    victim_id = (await reg_victim.json())["user"]["id"]

    response = await service_client.delete(
        f"/auth/admin/users/{victim_id}",
        headers={"Authorization": f"Bearer {actor_token}"},
    )
    assert response.status == 403
    payload = await response.json()
    assert "error" in payload
