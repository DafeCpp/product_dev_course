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



VIEWER_ROLE = "00000000-0000-0000-0000-000000000012"
EDITOR_ROLE = "00000000-0000-0000-0000-000000000011"


async def _project_with_member(service_client, prefix, member_roles=(VIEWER_ROLE,)):
    """Create owner, project and a member holding ``member_roles``."""
    owner_token = await get_auth_token(
        service_client, username=f"{prefix}owner", email=f"{prefix}owner@example.com",
    )
    member_token = await get_auth_token(
        service_client, username=f"{prefix}member", email=f"{prefix}member@example.com",
    )
    headers = {"Authorization": f"Bearer {owner_token}"}
    owner_id = (await (await service_client.get("/auth/me", headers=headers)).json())["id"]
    member_id = (await (await service_client.get(
        "/auth/me", headers={"Authorization": f"Bearer {member_token}"},
    )).json())["id"]
    create_response = await service_client.post(
        "/projects", headers=headers, json={"name": f"{prefix} project"},
    )
    assert create_response.status == 201
    project_id = (await create_response.json())["id"]
    for role_id in member_roles:
        add_response = await service_client.post(
            f"/projects/{project_id}/members",
            headers=headers,
            json={"user_id": member_id, "role_id": role_id},
        )
        assert add_response.status == 201
    return headers, project_id, owner_id, member_id


async def _member_roles(service_client, headers, project_id, user_id):
    response = await service_client.get(f"/projects/{project_id}/members", headers=headers)
    assert response.status == 200
    for member in (await response.json())["members"]:
        if member["user_id"] == user_id:
            return sorted(member["roles"])
    return None


@pytest.mark.asyncio
async def test_remove_member_without_role_removes_all_roles(service_client):
    headers, project_id, _owner_id, member_id = await _project_with_member(
        service_client, "rmall", member_roles=(VIEWER_ROLE, EDITOR_ROLE),
    )
    assert await _member_roles(service_client, headers, project_id, member_id) == ["editor", "viewer"]

    response = await service_client.delete(
        f"/api/v1/projects/{project_id}/members/{member_id}", headers=headers,
    )
    assert response.status == 200
    assert (await response.json())["ok"] is True
    assert await _member_roles(service_client, headers, project_id, member_id) is None


@pytest.mark.asyncio
async def test_remove_member_without_role_not_member(service_client):
    headers, project_id, _owner_id, member_id = await _project_with_member(
        service_client, "rmnone", member_roles=(),
    )
    response = await service_client.delete(
        f"/projects/{project_id}/members/{member_id}", headers=headers,
    )
    assert response.status == 404


@pytest.mark.asyncio
async def test_remove_owner_without_role_forbidden(service_client):
    headers, project_id, owner_id, _member_id = await _project_with_member(service_client, "rmown")
    response = await service_client.delete(
        f"/projects/{project_id}/members/{owner_id}", headers=headers,
    )
    assert response.status == 403
    assert await _member_roles(service_client, headers, project_id, owner_id) == ["owner"]


@pytest.mark.asyncio
async def test_remove_member_invalid_role_id(service_client):
    headers, project_id, _owner_id, member_id = await _project_with_member(service_client, "rmbad")
    response = await service_client.delete(
        f"/projects/{project_id}/members/{member_id}?role_id=not-a-uuid", headers=headers,
    )
    assert response.status == 400


@pytest.mark.asyncio
async def test_update_role_without_old_role_replaces_all_roles(service_client):
    headers, project_id, _owner_id, member_id = await _project_with_member(
        service_client, "upall", member_roles=(VIEWER_ROLE, EDITOR_ROLE),
    )
    response = await service_client.put(
        f"/api/v1/projects/{project_id}/members/{member_id}/role",
        headers=headers,
        json={"role": "viewer"},
    )
    assert response.status == 200
    assert (await response.json())["role_id"] == VIEWER_ROLE
    assert await _member_roles(service_client, headers, project_id, member_id) == ["viewer"]


@pytest.mark.asyncio
async def test_update_role_without_old_role_same_role_is_noop(service_client):
    headers, project_id, _owner_id, member_id = await _project_with_member(service_client, "upsame")
    response = await service_client.put(
        f"/projects/{project_id}/members/{member_id}/role",
        headers=headers,
        json={"role_id": VIEWER_ROLE},
    )
    assert response.status == 200
    assert (await response.json())["role_id"] == VIEWER_ROLE
    assert await _member_roles(service_client, headers, project_id, member_id) == ["viewer"]


@pytest.mark.asyncio
async def test_update_role_with_same_old_role_keeps_member(service_client):
    headers, project_id, _owner_id, member_id = await _project_with_member(service_client, "upkeep")
    response = await service_client.put(
        f"/projects/{project_id}/members/{member_id}/role?old_role=viewer",
        headers=headers,
        json={"role_id": VIEWER_ROLE},
    )
    assert response.status == 200
    assert await _member_roles(service_client, headers, project_id, member_id) == ["viewer"]


@pytest.mark.asyncio
async def test_update_owner_role_forbidden(service_client):
    headers, project_id, owner_id, _member_id = await _project_with_member(service_client, "upown")
    for query in ("", "?old_role=owner"):
        response = await service_client.put(
            f"/projects/{project_id}/members/{owner_id}/role{query}",
            headers=headers,
            json={"role_id": VIEWER_ROLE},
        )
        assert response.status == 403
    assert await _member_roles(service_client, headers, project_id, owner_id) == ["owner"]


@pytest.mark.asyncio
async def test_update_role_without_old_role_not_member(service_client):
    headers, project_id, _owner_id, member_id = await _project_with_member(
        service_client, "upnone", member_roles=(),
    )
    response = await service_client.put(
        f"/projects/{project_id}/members/{member_id}/role",
        headers=headers,
        json={"role_id": EDITOR_ROLE},
    )
    assert response.status == 404
    assert await _member_roles(service_client, headers, project_id, member_id) is None


@pytest.mark.asyncio
async def test_update_role_without_old_role_expired_member_not_found(service_client, database_url):
    import asyncpg

    headers, project_id, _owner_id, member_id = await _project_with_member(service_client, "upexp")
    conn = await asyncpg.connect(database_url)
    try:
        await conn.execute(
            "UPDATE user_project_roles SET expires_at = now() - interval '1 day' "
            "WHERE user_id = $1::uuid AND project_id = $2::uuid",
            member_id, project_id,
        )
    finally:
        await conn.close()

    response = await service_client.put(
        f"/projects/{project_id}/members/{member_id}/role",
        headers=headers,
        json={"role_id": EDITOR_ROLE},
    )
    assert response.status == 404
    assert await _member_roles(service_client, headers, project_id, member_id) is None


@pytest.mark.asyncio
async def test_concurrent_role_replacements_leave_one_role(service_client, database_url):
    import asyncio

    import asyncpg

    headers, project_id, _owner_id, member_id = await _project_with_member(
        service_client, "upconc", member_roles=(VIEWER_ROLE, EDITOR_ROLE),
    )

    async def put(role):
        return await service_client.put(
            f"/projects/{project_id}/members/{member_id}/role",
            headers=headers,
            json={"role": role},
        )

    responses = await asyncio.gather(*(put(r) for r in ("viewer", "editor") * 3))
    assert all(r.status == 200 for r in responses)

    conn = await asyncpg.connect(database_url)
    try:
        count = await conn.fetchval(
            "SELECT count(*) FROM user_project_roles WHERE user_id = $1::uuid AND project_id = $2::uuid",
            member_id, project_id,
        )
    finally:
        await conn.close()
    assert count == 1


@pytest.mark.asyncio
async def test_remove_member_without_role_expired_member_not_found(service_client, database_url):
    import asyncpg

    headers, project_id, _owner_id, member_id = await _project_with_member(service_client, "rmexp")
    conn = await asyncpg.connect(database_url)
    try:
        await conn.execute(
            "UPDATE user_project_roles SET expires_at = now() - interval '1 day' "
            "WHERE user_id = $1::uuid AND project_id = $2::uuid",
            member_id, project_id,
        )
    finally:
        await conn.close()

    response = await service_client.delete(
        f"/projects/{project_id}/members/{member_id}", headers=headers,
    )
    assert response.status == 404


@pytest.mark.asyncio
async def test_concurrent_remove_and_replace_never_keep_removed_member(service_client):
    import asyncio

    for attempt in range(5):
        headers, project_id, _owner_id, member_id = await _project_with_member(
            service_client, f"rmrace{attempt}", member_roles=(VIEWER_ROLE, EDITOR_ROLE),
        )
        remove, replace = await asyncio.gather(
            service_client.delete(f"/projects/{project_id}/members/{member_id}", headers=headers),
            service_client.put(
                f"/projects/{project_id}/members/{member_id}/role",
                headers=headers,
                json={"role": "viewer"},
            ),
        )
        # The member existed, so removal always succeeds; a replacement that
        # ran after it must see no member (404) and must not re-add the role.
        assert remove.status == 200
        assert replace.status in (200, 404)
        assert await _member_roles(service_client, headers, project_id, member_id) is None


@pytest.mark.asyncio
async def test_concurrent_remove_and_targeted_swap_never_keep_removed_member(service_client):
    import asyncio

    for attempt in range(5):
        headers, project_id, _owner_id, member_id = await _project_with_member(
            service_client, f"swrace{attempt}",
        )
        remove, swap = await asyncio.gather(
            service_client.delete(f"/projects/{project_id}/members/{member_id}", headers=headers),
            service_client.put(
                f"/projects/{project_id}/members/{member_id}/role?old_role=viewer",
                headers=headers,
                json={"role": "editor"},
            ),
        )
        assert remove.status == 200
        assert swap.status in (200, 404)
        assert await _member_roles(service_client, headers, project_id, member_id) is None


@pytest.mark.asyncio
async def test_same_role_replacement_keeps_expiry(service_client, database_url):
    import asyncpg

    headers, project_id, _owner_id, member_id = await _project_with_member(service_client, "upttl")
    conn = await asyncpg.connect(database_url)
    try:
        await conn.execute(
            "UPDATE user_project_roles SET expires_at = now() + interval '1 day' "
            "WHERE user_id = $1::uuid AND project_id = $2::uuid",
            member_id, project_id,
        )
        for query in ("", "?old_role=viewer"):
            response = await service_client.put(
                f"/projects/{project_id}/members/{member_id}/role{query}",
                headers=headers,
                json={"role_id": VIEWER_ROLE},
            )
            assert response.status == 200
            assert (await response.json())["expires_at"] is not None
        expires_at = await conn.fetchval(
            "SELECT expires_at FROM user_project_roles WHERE user_id = $1::uuid AND project_id = $2::uuid",
            member_id, project_id,
        )
    finally:
        await conn.close()
    assert expires_at is not None
