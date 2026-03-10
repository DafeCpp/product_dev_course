"""RBAC integration tests for experiment-service.

All tests rely on RBAC v2 headers injected by auth-proxy:
  X-User-Id, X-User-Is-Superadmin, X-User-Permissions

Tests cover:
  - 401 when headers are missing
  - 403 when required permission is absent
  - Superadmin bypasses all permission checks
  - Custom minimal permission set: viewer, editor
  - User with no project permissions → 403
  - Expired / empty permissions → 403
"""
from __future__ import annotations

import uuid

import pytest

from tests.utils import ROLE_PERMISSIONS, make_headers


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _custom_headers(
    project_id: uuid.UUID,
    permissions: str,
    *,
    user_id: uuid.UUID | None = None,
) -> dict[str, str]:
    """Build RBAC headers with an explicit comma-separated permissions string."""
    return {
        "X-User-Id": str(user_id or uuid.uuid4()),
        "X-Project-Id": str(project_id),
        "X-User-Is-Superadmin": "false",
        "X-User-Permissions": permissions,
    }


async def _create_experiment(service_client, project_id: uuid.UUID, name: str = "Test") -> str:
    headers = make_headers(project_id, role="owner")
    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": name},
        headers=headers,
    )
    assert resp.status == 201, await resp.text()
    return str((await resp.json())["id"])


async def _create_sensor(service_client, project_id: uuid.UUID, name: str = "sensor") -> str:
    headers = make_headers(project_id, role="owner")
    resp = await service_client.post(
        "/api/v1/sensors",
        json={
            "project_id": str(project_id),
            "name": name,
            "type": "thermocouple",
            "input_unit": "mV",
            "display_unit": "C",
        },
        headers=headers,
    )
    assert resp.status == 201, await resp.text()
    return str((await resp.json())["sensor"]["id"])


# ---------------------------------------------------------------------------
# 401 — missing headers
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_missing_headers_return_401(service_client):
    resp = await service_client.get("/api/v1/experiments")
    assert resp.status == 401


@pytest.mark.asyncio
async def test_missing_user_id_returns_401(service_client):
    """X-User-Permissions без X-User-Id → 401."""
    project_id = uuid.uuid4()
    resp = await service_client.get(
        "/api/v1/experiments",
        headers={
            "X-User-Is-Superadmin": "false",
            "X-Project-Id": str(project_id),
            "X-User-Permissions": "experiments.view",
        },
    )
    assert resp.status == 401


# ---------------------------------------------------------------------------
# Viewer (experiments.view + project.members.view)
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_viewer_can_list_experiments(service_client):
    project_id = uuid.uuid4()
    await _create_experiment(service_client, project_id, "Visible")
    viewer_headers = make_headers(project_id, role="viewer")

    resp = await service_client.get(
        f"/api/v1/experiments?project_id={project_id}",
        headers=viewer_headers,
    )
    assert resp.status == 200


@pytest.mark.asyncio
async def test_viewer_cannot_create_experiment(service_client):
    project_id = uuid.uuid4()
    viewer_headers = make_headers(project_id, role="viewer")

    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "Forbidden"},
        headers=viewer_headers,
    )
    assert resp.status == 403


@pytest.mark.asyncio
async def test_viewer_cannot_create_run(service_client):
    project_id = uuid.uuid4()
    experiment_id = await _create_experiment(service_client, project_id, "RBAC experiment")

    viewer_headers = make_headers(project_id, role="viewer")
    resp = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/runs",
        json={"name": "viewer-run"},
        headers=viewer_headers,
    )
    assert resp.status == 403


@pytest.mark.asyncio
async def test_viewer_cannot_rotate_sensor_token(service_client):
    project_id = uuid.uuid4()
    sensor_id = await _create_sensor(service_client, project_id, "rbac-sensor")

    viewer_headers = make_headers(project_id, role="viewer")
    resp = await service_client.post(
        f"/api/v1/sensors/{sensor_id}/rotate-token",
        headers=viewer_headers,
    )
    assert resp.status == 403


@pytest.mark.asyncio
async def test_viewer_cannot_update_experiment(service_client):
    project_id = uuid.uuid4()
    experiment_id = await _create_experiment(service_client, project_id, "To update")

    viewer_headers = make_headers(project_id, role="viewer")
    resp = await service_client.patch(
        f"/api/v1/experiments/{experiment_id}",
        json={"name": "New name"},
        headers=viewer_headers,
        params={"project_id": str(project_id)},
    )
    assert resp.status == 403


# ---------------------------------------------------------------------------
# Editor
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_editor_can_create_experiment(service_client):
    project_id = uuid.uuid4()
    editor_headers = make_headers(project_id, role="editor")

    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "Editor experiment"},
        headers=editor_headers,
    )
    assert resp.status == 201


@pytest.mark.asyncio
async def test_editor_can_create_run(service_client):
    project_id = uuid.uuid4()
    experiment_id = await _create_experiment(service_client, project_id, "Editor run exp")

    editor_headers = make_headers(project_id, role="editor")
    resp = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/runs",
        json={"name": "editor-run"},
        headers=editor_headers,
    )
    assert resp.status == 201


@pytest.mark.asyncio
async def test_editor_cannot_publish_conversion_profile(service_client):
    project_id = uuid.uuid4()
    sensor_id = await _create_sensor(service_client, project_id, "sensor-rbac")

    owner_headers = make_headers(project_id, role="owner")
    resp_profile = await service_client.post(
        f"/api/v1/sensors/{sensor_id}/conversion-profiles",
        json={"version": "v1", "kind": "linear", "payload": {"a": 1, "b": 2}},
        headers=owner_headers,
    )
    assert resp_profile.status == 201
    profile_id = (await resp_profile.json())["id"]

    editor_headers = make_headers(project_id, role="editor")
    resp_publish = await service_client.post(
        f"/api/v1/sensors/{sensor_id}/conversion-profiles/{profile_id}/publish",
        headers=editor_headers,
    )
    assert resp_publish.status == 403


@pytest.mark.asyncio
async def test_editor_cannot_delete_experiment(service_client):
    project_id = uuid.uuid4()
    experiment_id = await _create_experiment(service_client, project_id, "Delete test")

    editor_headers = make_headers(project_id, role="editor")
    resp = await service_client.delete(
        f"/api/v1/experiments/{experiment_id}",
        headers=editor_headers,
        params={"project_id": str(project_id)},
    )
    assert resp.status == 403


# ---------------------------------------------------------------------------
# Superadmin bypasses all checks
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_superadmin_can_create_experiment(service_client):
    project_id = uuid.uuid4()
    sa_headers = make_headers(project_id, superadmin=True)

    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "SA experiment"},
        headers=sa_headers,
    )
    assert resp.status == 201


@pytest.mark.asyncio
async def test_superadmin_can_delete_experiment(service_client):
    project_id = uuid.uuid4()
    experiment_id = await _create_experiment(service_client, project_id, "SA delete")

    sa_headers = make_headers(project_id, superadmin=True)
    resp = await service_client.delete(
        f"/api/v1/experiments/{experiment_id}",
        headers=sa_headers,
        params={"project_id": str(project_id)},
    )
    assert resp.status == 204


@pytest.mark.asyncio
async def test_superadmin_can_publish_conversion_profile(service_client):
    project_id = uuid.uuid4()
    sa_headers = make_headers(project_id, superadmin=True)
    sensor_id = await _create_sensor(service_client, project_id, "sa-sensor")

    resp_profile = await service_client.post(
        f"/api/v1/sensors/{sensor_id}/conversion-profiles",
        json={"version": "v1", "kind": "linear", "payload": {"a": 1, "b": 2}},
        headers=sa_headers,
    )
    assert resp_profile.status == 201
    profile_id = (await resp_profile.json())["id"]

    resp_publish = await service_client.post(
        f"/api/v1/sensors/{sensor_id}/conversion-profiles/{profile_id}/publish",
        headers=sa_headers,
    )
    assert resp_publish.status == 200


# ---------------------------------------------------------------------------
# User with no project permissions → 403
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_no_permissions_cannot_list_experiments(service_client):
    project_id = uuid.uuid4()
    no_perm_headers = _custom_headers(project_id, permissions="")

    resp = await service_client.get(
        f"/api/v1/experiments?project_id={project_id}",
        headers=no_perm_headers,
    )
    assert resp.status == 403


@pytest.mark.asyncio
async def test_no_permissions_cannot_create_experiment(service_client):
    project_id = uuid.uuid4()
    no_perm_headers = _custom_headers(project_id, permissions="")

    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "Should fail"},
        headers=no_perm_headers,
    )
    assert resp.status == 403


# ---------------------------------------------------------------------------
# Custom minimal permission set
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_custom_view_only_can_list_but_not_create(service_client):
    """User with only experiments.view can list but not create."""
    project_id = uuid.uuid4()
    await _create_experiment(service_client, project_id, "Existing")

    view_headers = _custom_headers(project_id, permissions="experiments.view")
    list_resp = await service_client.get(
        f"/api/v1/experiments?project_id={project_id}",
        headers=view_headers,
    )
    assert list_resp.status == 200

    create_resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "Forbidden"},
        headers=view_headers,
    )
    assert create_resp.status == 403


@pytest.mark.asyncio
async def test_custom_create_only_can_create_but_not_list(service_client):
    """User with only experiments.create cannot list (needs experiments.view)."""
    project_id = uuid.uuid4()
    create_headers = _custom_headers(project_id, permissions="experiments.create")

    create_resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "Created"},
        headers=create_headers,
    )
    assert create_resp.status == 201

    list_resp = await service_client.get(
        f"/api/v1/experiments?project_id={project_id}",
        headers=create_headers,
    )
    assert list_resp.status == 403


@pytest.mark.asyncio
async def test_custom_runs_create_without_experiments_view(service_client):
    """User with runs.create but no experiments.view cannot list experiments."""
    project_id = uuid.uuid4()
    experiment_id = await _create_experiment(service_client, project_id, "runs test")

    run_headers = _custom_headers(project_id, permissions="runs.create,experiments.create")
    run_resp = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/runs",
        json={"name": "custom-run"},
        headers=run_headers,
    )
    assert run_resp.status == 201

    list_resp = await service_client.get(
        f"/api/v1/experiments?project_id={project_id}",
        headers=run_headers,
    )
    assert list_resp.status == 403


# ---------------------------------------------------------------------------
# Expired / garbage permissions → 403
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_garbage_permissions_are_ignored(service_client):
    """Unrecognised permission strings do not grant access."""
    project_id = uuid.uuid4()
    bad_headers = _custom_headers(project_id, permissions="admin,superuser,*,ALL")

    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "Should fail"},
        headers=bad_headers,
    )
    assert resp.status == 403


@pytest.mark.asyncio
async def test_whitespace_only_permissions_are_denied(service_client):
    """Whitespace-only permissions header → effectively empty → 403."""
    project_id = uuid.uuid4()
    bad_headers = _custom_headers(project_id, permissions="   ,  ,  ")

    resp = await service_client.get(
        f"/api/v1/experiments?project_id={project_id}",
        headers=bad_headers,
    )
    assert resp.status == 403


# ---------------------------------------------------------------------------
# Owner full access
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_owner_full_lifecycle(service_client):
    """Owner can create, update, archive, and delete an experiment."""
    project_id = uuid.uuid4()
    owner_headers = make_headers(project_id, role="owner")

    # Create
    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "Lifecycle"},
        headers=owner_headers,
    )
    assert resp.status == 201
    exp_id = (await resp.json())["id"]

    # Update
    resp = await service_client.patch(
        f"/api/v1/experiments/{exp_id}",
        json={"name": "Lifecycle v2"},
        headers=owner_headers,
        params={"project_id": str(project_id)},
    )
    assert resp.status == 200

    # Archive
    resp = await service_client.post(
        f"/api/v1/experiments/{exp_id}/archive",
        headers=owner_headers,
        params={"project_id": str(project_id)},
    )
    assert resp.status == 200

    # Delete
    resp = await service_client.delete(
        f"/api/v1/experiments/{exp_id}",
        headers=owner_headers,
        params={"project_id": str(project_id)},
    )
    assert resp.status == 204
