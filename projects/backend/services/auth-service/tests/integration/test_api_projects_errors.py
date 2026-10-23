"""Error-path tests for the project routes: auth, bad input, missing resources, 500s."""
from __future__ import annotations

import uuid

import pytest

from auth_service.services.projects import ProjectService

MISSING = str(uuid.uuid4())
ROLE_VIEWER = "viewer"

# (method, path template, json body) for every project endpoint.
ENDPOINTS = [
    ("POST", "/projects", {"name": "p"}),
    ("GET", "/projects", None),
    ("GET", "/projects/{pid}", None),
    ("PUT", "/projects/{pid}", {"name": "n"}),
    ("DELETE", "/projects/{pid}", None),
    ("GET", "/projects/{pid}/members", None),
    ("POST", "/projects/{pid}/members", {"user_id": MISSING, "role": ROLE_VIEWER}),
    ("DELETE", "/projects/{pid}/members/{uid}", None),
    ("PUT", "/projects/{pid}/members/{uid}/role", {"role": ROLE_VIEWER}),
]
ENDPOINT_IDS = [f"{m} {p}" for m, p, _ in ENDPOINTS]
PROJECT_ENDPOINTS = [e for e in ENDPOINTS if "{pid}" in e[1]]
PROJECT_ENDPOINT_IDS = [f"{m} {p}" for m, p, _ in PROJECT_ENDPOINTS]


async def register(client, name: str) -> tuple[str, str]:
    response = await client.post(
        "/auth/register",
        json={"username": name, "email": f"{name}@example.com", "password": "testpass123"},
    )
    assert response.status == 201
    payload = await response.json()
    return payload["access_token"], payload["user"]["id"]


def auth(token: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {token}"}


def build(path: str, pid: str = MISSING, uid: str = MISSING) -> str:
    return path.format(pid=pid, uid=uid)


async def create_project(client, token: str, name: str = "Project") -> str:
    response = await client.post("/projects", headers=auth(token), json={"name": name})
    assert response.status == 201
    return (await response.json())["id"]


@pytest.mark.parametrize("method,path,body", ENDPOINTS, ids=ENDPOINT_IDS)
async def test_endpoint_requires_authentication(service_client, method, path, body):
    response = await service_client.request(method, build(path), json=body)
    assert response.status == 401


@pytest.mark.parametrize("method,path,body", PROJECT_ENDPOINTS, ids=PROJECT_ENDPOINT_IDS)
async def test_invalid_project_id_is_rejected(service_client, method, path, body):
    token, _ = await register(service_client, "badpid")
    url = build(path, pid="not-a-uuid")
    response = await service_client.request(method, url, headers=auth(token), json=body)
    assert response.status == 400


@pytest.mark.parametrize("method,path,body", PROJECT_ENDPOINTS, ids=PROJECT_ENDPOINT_IDS)
async def test_unknown_project_is_not_accessible(service_client, method, path, body):
    token, _ = await register(service_client, "outsider")
    response = await service_client.request(
        method, build(path), headers=auth(token), json=body
    )
    assert response.status in (403, 404)


@pytest.mark.parametrize("method,path,body", PROJECT_ENDPOINTS, ids=PROJECT_ENDPOINT_IDS)
async def test_unexpected_service_error_returns_500(
    service_client, monkeypatch, method, path, body
):
    token, _ = await register(service_client, "boom")

    async def explode(*args, **kwargs):
        raise RuntimeError("database is on fire")

    for name in (
        "get_project",
        "update_project",
        "delete_project",
        "list_members",
        "add_member",
        "remove_member",
        "update_member_role",
    ):
        monkeypatch.setattr(ProjectService, name, explode)

    response = await service_client.request(
        method, build(path), headers=auth(token), json=body
    )
    assert response.status == 500
    assert (await response.json())["error"] == "Internal server error"


async def test_create_and_list_report_unexpected_errors(service_client, monkeypatch):
    token, _ = await register(service_client, "boomcreate")

    async def explode(*args, **kwargs):
        raise RuntimeError("database is on fire")

    monkeypatch.setattr(ProjectService, "create_project", explode)
    monkeypatch.setattr(ProjectService, "list_user_projects", explode)

    created = await service_client.post("/projects", headers=auth(token), json={"name": "x"})
    listed = await service_client.get("/projects", headers=auth(token))
    assert created.status == 500
    assert listed.status == 500


@pytest.mark.parametrize(
    "method,path,body",
    [
        ("POST", "/projects", {}),
        ("POST", "/projects", {"name": ""}),
        ("PUT", "/projects/{pid}", {"name": ""}),
        ("POST", "/projects/{pid}/members", {}),
        ("PUT", "/projects/{pid}/members/{uid}/role", {"role_id": 5}),
    ],
)
async def test_invalid_body_is_rejected(service_client, method, path, body):
    token, _ = await register(service_client, "badbody")
    response = await service_client.request(
        method, build(path), headers=auth(token), json=body
    )
    assert response.status == 400


async def test_list_projects_rejects_non_integer_paging(service_client):
    token, _ = await register(service_client, "paging")
    response = await service_client.get("/projects?limit=ten", headers=auth(token))
    assert response.status == 400


async def test_list_projects_clamps_paging_and_filters(service_client):
    token, _ = await register(service_client, "clamp")
    await create_project(service_client, token, "Alpha")
    await create_project(service_client, token, "Beta")

    response = await service_client.get(
        "/projects?limit=1000&offset=-5&search=alp&role=owner", headers=auth(token)
    )
    assert response.status == 200
    payload = await response.json()
    assert payload["limit"] == 100
    assert payload["offset"] == 0
    assert [p["name"] for p in payload["items"]] == ["Alpha"]


async def test_non_member_cannot_read_update_or_delete_project(service_client):
    owner_token, _ = await register(service_client, "projowner")
    other_token, _ = await register(service_client, "projother")
    project_id = await create_project(service_client, owner_token)

    for method, path, body in (
        ("GET", f"/projects/{project_id}", None),
        ("PUT", f"/projects/{project_id}", {"name": "stolen"}),
        ("DELETE", f"/projects/{project_id}", None),
        ("GET", f"/projects/{project_id}/members", None),
    ):
        response = await service_client.request(
            method, path, headers=auth(other_token), json=body
        )
        assert response.status == 403, (method, path)


async def test_add_member_rejects_malformed_ids_and_roles(service_client):
    token, _ = await register(service_client, "addbad")
    project_id = await create_project(service_client, token)
    url = f"/projects/{project_id}/members"

    bad_user = await service_client.post(
        url, headers=auth(token), json={"user_id": "nope", "role": ROLE_VIEWER}
    )
    unknown_role = await service_client.post(
        url, headers=auth(token), json={"user_id": MISSING, "role": "emperor"}
    )
    no_role = await service_client.post(url, headers=auth(token), json={"user_id": MISSING})
    assert bad_user.status == 400
    assert unknown_role.status == 400
    assert no_role.status == 400


async def test_add_member_unknown_user_is_not_found(service_client):
    token, _ = await register(service_client, "addghost")
    project_id = await create_project(service_client, token)
    response = await service_client.post(
        f"/projects/{project_id}/members",
        headers=auth(token),
        json={"user_id": MISSING, "role": ROLE_VIEWER},
    )
    assert response.status == 404


async def test_member_lifecycle_through_api(service_client):
    owner_token, _ = await register(service_client, "lifeowner")
    _, member_id = await register(service_client, "lifemember")
    project_id = await create_project(service_client, owner_token)

    added = await service_client.post(
        f"/projects/{project_id}/members",
        headers=auth(owner_token),
        json={"user_id": member_id, "role": ROLE_VIEWER},
    )
    assert added.status == 201
    assert (await added.json())["user_id"] == member_id

    listed = await service_client.get(
        f"/projects/{project_id}/members", headers=auth(owner_token)
    )
    members = (await listed.json())["members"]
    assert {m["user_id"] for m in members} >= {member_id}

    promoted = await service_client.put(
        f"/projects/{project_id}/members/{member_id}/role?old_role=viewer",
        headers=auth(owner_token),
        json={"role": "editor"},
    )
    assert promoted.status == 200

    removed_role = await service_client.delete(
        f"/projects/{project_id}/members/{member_id}?role_id=00000000-0000-0000-0000-0000000000a2",
        headers=auth(owner_token),
    )
    assert removed_role.status == 200

    removed = await service_client.delete(
        f"/projects/{project_id}/members/{member_id}", headers=auth(owner_token)
    )
    assert removed.status == 200


async def test_remove_member_validates_ids_and_body(service_client):
    token, _ = await register(service_client, "rmbad")
    project_id = await create_project(service_client, token)

    bad_ids = await service_client.delete(
        f"/projects/{project_id}/members/not-a-uuid", headers=auth(token)
    )
    bad_role = await service_client.delete(
        f"/projects/{project_id}/members/{MISSING}?role_id=nope", headers=auth(token)
    )
    bad_body_role = await service_client.delete(
        f"/projects/{project_id}/members/{MISSING}",
        headers=auth(token),
        json={"role_id": "nope"},
    )
    assert bad_ids.status == 400
    assert bad_role.status == 400
    assert bad_body_role.status == 400


async def test_update_member_role_validates_ids_and_old_role(service_client):
    token, _ = await register(service_client, "rolebad")
    project_id = await create_project(service_client, token)

    bad_ids = await service_client.put(
        f"/projects/{project_id}/members/not-a-uuid/role",
        headers=auth(token),
        json={"role": ROLE_VIEWER},
    )
    bad_old_role = await service_client.put(
        f"/projects/{project_id}/members/{MISSING}/role?old_role=nope",
        headers=auth(token),
        json={"role": ROLE_VIEWER},
    )
    assert bad_ids.status == 400
    assert bad_old_role.status == 400
