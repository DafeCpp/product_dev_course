"""Project API tests."""
import pytest


async def get_auth_token(service_client, username="testuser", email="test@example.com", password="testpass123"):
    """Helper to register and get auth token."""
    # Register user
    register_response = await service_client.post(
        "/auth/register",
        json={
            "username": username,
            "email": email,
            "password": password,
        },
    )
    assert register_response.status == 201
    register_payload = await register_response.json()
    return register_payload["access_token"]


@pytest.mark.asyncio
async def test_create_project_success(service_client):
    """Test successful project creation."""
    token = await get_auth_token(service_client)

    response = await service_client.post(
        "/projects",
        headers={"Authorization": f"Bearer {token}"},
        json={
            "name": "Test Project",
            "description": "Test description",
        },
    )
    assert response.status == 201
    payload = await response.json()
    assert payload["name"] == "Test Project"
    assert payload["description"] == "Test description"
    assert "id" in payload
    assert "owner_id" in payload
    assert "created_at" in payload
    assert "updated_at" in payload


@pytest.mark.asyncio
async def test_create_project_unauthorized(service_client):
    """Test project creation without token."""
    response = await service_client.post(
        "/projects",
        json={
            "name": "Test Project",
            "description": "Test description",
        },
    )
    assert response.status == 401


@pytest.mark.asyncio
async def test_list_projects_success(service_client):
    """Test listing user projects."""
    token = await get_auth_token(service_client, username="listuser", email="list@example.com")

    # Create a project
    create_response = await service_client.post(
        "/projects",
        headers={"Authorization": f"Bearer {token}"},
        json={
            "name": "Project 1",
            "description": "First project",
        },
    )
    assert create_response.status == 201

    # Create another project
    create_response = await service_client.post(
        "/projects",
        headers={"Authorization": f"Bearer {token}"},
        json={
            "name": "Project 2",
            "description": "Second project",
        },
    )
    assert create_response.status == 201

    # List projects
    response = await service_client.get(
        "/projects",
        headers={"Authorization": f"Bearer {token}"},
    )
    assert response.status == 200
    payload = await response.json()
    assert "items" in payload
    assert len(payload["items"]) == 2


@pytest.mark.asyncio
async def test_list_projects_empty(service_client):
    """Test listing projects when user has none."""
    token = await get_auth_token(service_client, username="emptyuser", email="empty@example.com")

    response = await service_client.get(
        "/projects",
        headers={"Authorization": f"Bearer {token}"},
    )
    assert response.status == 200
    payload = await response.json()
    assert "items" in payload
    assert len(payload["items"]) == 0


@pytest.mark.asyncio
async def test_get_project_success(service_client):
    """Test getting project by ID."""
    token = await get_auth_token(service_client, username="getuser", email="get@example.com")

    # Create project
    create_response = await service_client.post(
        "/projects",
        headers={"Authorization": f"Bearer {token}"},
        json={
            "name": "Get Project",
            "description": "Project to get",
        },
    )
    assert create_response.status == 201
    project_id = (await create_response.json())["id"]

    # Get project
    response = await service_client.get(
        f"/projects/{project_id}",
        headers={"Authorization": f"Bearer {token}"},
    )
    assert response.status == 200
    payload = await response.json()
    assert payload["id"] == project_id
    assert payload["name"] == "Get Project"


@pytest.mark.asyncio
async def test_get_project_not_found(service_client):
    """Test getting non-existent project."""
    token = await get_auth_token(service_client, username="notfound", email="notfound@example.com")

    response = await service_client.get(
        "/projects/00000000-0000-0000-0000-000000000000",
        headers={"Authorization": f"Bearer {token}"},
    )
    # RBAC returns 403 Forbidden for non-existent projects when user has no access
    assert response.status == 403


@pytest.mark.asyncio
async def test_update_project_success(service_client):
    """Test successful project update."""
    token = await get_auth_token(service_client, username="updateuser", email="update@example.com")

    # Create project
    create_response = await service_client.post(
        "/projects",
        headers={"Authorization": f"Bearer {token}"},
        json={
            "name": "Original Name",
            "description": "Original description",
        },
    )
    assert create_response.status == 201
    project_id = (await create_response.json())["id"]

    # Update project
    response = await service_client.put(
        f"/projects/{project_id}",
        headers={"Authorization": f"Bearer {token}"},
        json={
            "name": "Updated Name",
            "description": "Updated description",
        },
    )
    assert response.status == 200
    payload = await response.json()
    assert payload["name"] == "Updated Name"
    assert payload["description"] == "Updated description"


@pytest.mark.asyncio
async def test_update_project_partial(service_client):
    """Test partial project update."""
    token = await get_auth_token(service_client, username="partialuser", email="partial@example.com")

    # Create project
    create_response = await service_client.post(
        "/projects",
        headers={"Authorization": f"Bearer {token}"},
        json={
            "name": "Original Name",
            "description": "Original description",
        },
    )
    assert create_response.status == 201
    project_id = (await create_response.json())["id"]

    # Update only name
    response = await service_client.put(
        f"/projects/{project_id}",
        headers={"Authorization": f"Bearer {token}"},
        json={
            "name": "Updated Name",
        },
    )
    assert response.status == 200
    payload = await response.json()
    assert payload["name"] == "Updated Name"
    assert payload["description"] == "Original description"


@pytest.mark.asyncio
async def test_delete_project_success(service_client):
    """Test successful project deletion."""
    token = await get_auth_token(service_client, username="deleteuser", email="delete@example.com")

    # Create project
    create_response = await service_client.post(
        "/projects",
        headers={"Authorization": f"Bearer {token}"},
        json={
            "name": "To Delete",
            "description": "Will be deleted",
        },
    )
    assert create_response.status == 201
    project_id = (await create_response.json())["id"]

    # Delete project
    response = await service_client.delete(
        f"/projects/{project_id}",
        headers={"Authorization": f"Bearer {token}"},
    )
    assert response.status == 200
    payload = await response.json()
    assert payload["ok"] is True

    # Verify project is deleted
    get_response = await service_client.get(
        f"/projects/{project_id}",
        headers={"Authorization": f"Bearer {token}"},
    )
    # RBAC returns 403 Forbidden for deleted/non-existent projects when user has no access
    assert get_response.status == 403


@pytest.mark.asyncio
async def test_list_members_success(service_client):
    """Test listing project members."""
    token = await get_auth_token(service_client, username="membersuser", email="members@example.com")

    # Create project
    create_response = await service_client.post(
        "/projects",
        headers={"Authorization": f"Bearer {token}"},
        json={
            "name": "Members Project",
            "description": "Project with members",
        },
    )
    assert create_response.status == 201
    project_id = (await create_response.json())["id"]

    # List members (should include owner)
    response = await service_client.get(
        f"/projects/{project_id}/members",
        headers={"Authorization": f"Bearer {token}"},
    )
    assert response.status == 200
    payload = await response.json()
    assert "members" in payload
    assert len(payload["members"]) == 1
    # Owner role is assigned as a list
    assert "owner" in payload["members"][0]["roles"]


@pytest.mark.asyncio
async def test_add_member_success(service_client):
    """Test adding member to project."""
    # Create owner
    owner_token = await get_auth_token(service_client, username="owner", email="owner@example.com")

    # Create another user to add as member
    member_token = await get_auth_token(service_client, username="member", email="member@example.com")

    # Get owner user ID
    owner_me = await service_client.get(
        "/auth/me",
        headers={"Authorization": f"Bearer {owner_token}"},
    )
    owner_id = (await owner_me.json())["id"]

    # Get member user ID
    member_me = await service_client.get(
        "/auth/me",
        headers={"Authorization": f"Bearer {member_token}"},
    )
    member_id = (await member_me.json())["id"]

    # Create project
    create_response = await service_client.post(
        "/projects",
        headers={"Authorization": f"Bearer {owner_token}"},
        json={
            "name": "Add Member Project",
            "description": "Project to add member",
        },
    )
    assert create_response.status == 201
    project_id = (await create_response.json())["id"]

    # Add member (use viewer role UUID)
    response = await service_client.post(
        f"/projects/{project_id}/members",
        headers={"Authorization": f"Bearer {owner_token}"},
        json={
            "user_id": member_id,
            "role_id": "00000000-0000-0000-0000-000000000012",  # viewer role
        },
    )
    assert response.status == 201
    payload = await response.json()
    assert payload["user_id"] == member_id
    assert payload["role_id"] == "00000000-0000-0000-0000-000000000012"

    # Verify member is in list
    list_response = await service_client.get(
        f"/projects/{project_id}/members",
        headers={"Authorization": f"Bearer {owner_token}"},
    )
    assert list_response.status == 200
    list_payload = await list_response.json()
    assert len(list_payload["members"]) == 2


@pytest.mark.asyncio
async def test_remove_member_success(service_client):
    """Test removing member from project."""
    # Create owner
    owner_token = await get_auth_token(service_client, username="removeowner", email="removeowner@example.com")

    # Create member
    member_token = await get_auth_token(service_client, username="removemember", email="removemember@example.com")

    # Get member user ID
    member_me = await service_client.get(
        "/auth/me",
        headers={"Authorization": f"Bearer {member_token}"},
    )
    member_id = (await member_me.json())["id"]

    # Create project
    create_response = await service_client.post(
        "/projects",
        headers={"Authorization": f"Bearer {owner_token}"},
        json={
            "name": "Remove Member Project",
            "description": "Project to remove member",
        },
    )
    assert create_response.status == 201
    project_id = (await create_response.json())["id"]

    # Add member (use viewer role UUID)
    await service_client.post(
        f"/projects/{project_id}/members",
        headers={"Authorization": f"Bearer {owner_token}"},
        json={
            "user_id": member_id,
            "role_id": "00000000-0000-0000-0000-000000000012",  # viewer role
        },
    )

    # Remove member (role_id required)
    response = await service_client.delete(
        f"/projects/{project_id}/members/{member_id}?role_id=00000000-0000-0000-0000-000000000012",
        headers={"Authorization": f"Bearer {owner_token}"},
    )
    assert response.status == 200
    payload = await response.json()
    assert payload["ok"] is True

    # Verify member is removed
    list_response = await service_client.get(
        f"/projects/{project_id}/members",
        headers={"Authorization": f"Bearer {owner_token}"},
    )
    assert list_response.status == 200
    list_payload = await list_response.json()
    assert len(list_payload["members"]) == 1  # Only owner remains


@pytest.mark.asyncio
async def test_update_member_role_success(service_client):
    """Test updating member role."""
    # Create owner
    owner_token = await get_auth_token(service_client, username="roleowner", email="roleowner@example.com")

    # Create member
    member_token = await get_auth_token(service_client, username="rolemember", email="rolemember@example.com")

    # Get member user ID
    member_me = await service_client.get(
        "/auth/me",
        headers={"Authorization": f"Bearer {member_token}"},
    )
    member_id = (await member_me.json())["id"]

    # Create project
    create_response = await service_client.post(
        "/projects",
        headers={"Authorization": f"Bearer {owner_token}"},
        json={
            "name": "Role Update Project",
            "description": "Project to update role",
        },
    )
    assert create_response.status == 201
    project_id = (await create_response.json())["id"]

    # Add member as viewer (use viewer role UUID)
    await service_client.post(
        f"/projects/{project_id}/members",
        headers={"Authorization": f"Bearer {owner_token}"},
        json={
            "user_id": member_id,
            "role_id": "00000000-0000-0000-0000-000000000012",  # viewer role
        },
    )

    # Update role to editor (use editor role UUID)
    response = await service_client.put(
        f"/projects/{project_id}/members/{member_id}/role?old_role_id=00000000-0000-0000-0000-000000000012",
        headers={"Authorization": f"Bearer {owner_token}"},
        json={
            "role_id": "00000000-0000-0000-0000-000000000011",  # editor role
        },
    )
    assert response.status == 200
    payload = await response.json()
    assert payload["role_id"] == "00000000-0000-0000-0000-000000000011"

