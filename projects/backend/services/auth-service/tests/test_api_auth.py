"""Authentication API tests."""
import pytest


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
    """Test logout endpoint."""
    response = await service_client.post("/auth/logout")
    assert response.status == 200
    payload = await response.json()
    assert payload["ok"] is True


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

