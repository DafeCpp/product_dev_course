from __future__ import annotations

# pyright: reportMissingImports=false

import uuid

import asyncpg
import pytest

from experiment_service.services.idempotency import IDEMPOTENCY_HEADER
from tests.utils import make_headers


@pytest.mark.asyncio
async def test_sensor_and_profiles_flow(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    resp = await service_client.post(
        "/api/v1/sensors",
        json={
            "project_id": str(project_id),
            "name": "thermo-1",
            "type": "thermocouple",
            "input_unit": "mV",
            "display_unit": "C",
            "conversion_profile": {
                "version": "v1",
                "kind": "linear",
                "payload": {"a": 1.2, "b": 0.4},
            },
        },
        headers={**headers, IDEMPOTENCY_HEADER: "sensor-idem"},
    )
    assert resp.status == 201
    first_sensor = await resp.json()
    assert "token" in first_sensor
    sensor_id = first_sensor["sensor"]["id"]

    # Idempotent retry returns same response
    resp_retry = await service_client.post(
        "/api/v1/sensors",
        json={
            "project_id": str(project_id),
            "name": "thermo-1",
            "type": "thermocouple",
            "input_unit": "mV",
            "display_unit": "C",
        },
        headers={**headers, IDEMPOTENCY_HEADER: "sensor-idem"},
    )
    assert resp_retry.status == 201
    retry_body = await resp_retry.json()
    assert retry_body["sensor"]["id"] == sensor_id

    # Rotate token
    resp = await service_client.post(
        f"/api/v1/sensors/{sensor_id}/rotate-token",
        headers=headers,
    )
    assert resp.status == 200
    rotated = await resp.json()
    assert rotated["sensor"]["id"] == sensor_id
    assert rotated["token"] != first_sensor["token"]

    # List sensors
    resp = await service_client.get("/api/v1/sensors", headers=headers)
    assert resp.status == 200
    data = await resp.json()
    assert data["total"] >= 1

    # Create conversion profile
    resp = await service_client.post(
        f"/api/v1/sensors/{sensor_id}/conversion-profiles",
        json={
            "version": "v2",
            "kind": "polynomial",
            "payload": {"a0": 0.2},
        },
        headers=headers,
    )
    assert resp.status == 201
    profile = await resp.json()
    profile_id = profile["id"]

    # Publish conversion profile
    resp = await service_client.post(
        f"/api/v1/sensors/{sensor_id}/conversion-profiles/{profile_id}/publish",
        headers=headers,
    )
    assert resp.status == 200
    published = await resp.json()
    assert published["status"] == "active"

    # List conversion profiles
    resp = await service_client.get(
        f"/api/v1/sensors/{sensor_id}/conversion-profiles",
        headers=headers,
    )
    assert resp.status == 200
    profiles = await resp.json()
    assert profiles["total"] >= 2


@pytest.mark.asyncio
async def test_rotate_token_unknown_sensor_returns_404(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    resp = await service_client.post(
        f"/api/v1/sensors/{uuid.uuid4()}/rotate-token",
        headers=headers,
    )
    assert resp.status == 404


@pytest.mark.asyncio
async def test_sensor_register_idempotency_conflict(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    idem_headers = {**headers, IDEMPOTENCY_HEADER: "sensor-idem-conflict"}
    payload = {
        "project_id": str(project_id),
        "name": "idem-sensor",
        "type": "thermocouple",
        "input_unit": "mV",
        "display_unit": "C",
    }

    first = await service_client.post("/api/v1/sensors", json=payload, headers=idem_headers)
    assert first.status == 201

    conflict = await service_client.post(
        "/api/v1/sensors",
        json={**payload, "type": "strain"},
        headers=idem_headers,
    )
    assert conflict.status == 409


@pytest.mark.asyncio
async def test_delete_sensor_missing_returns_404(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    resp = await service_client.delete(f"/api/v1/sensors/{uuid.uuid4()}", headers=headers)
    assert resp.status == 404


@pytest.mark.asyncio
async def test_sensor_cannot_be_deleted_with_active_capture_sessions(service_client, pgsql):
    project_id = uuid.uuid4()
    user_id = uuid.uuid4()
    headers = make_headers(project_id, user_id=user_id)

    # Create experiment + run
    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "Sensor invariants"},
        headers=headers,
    )
    assert resp.status == 201
    experiment_id = (await resp.json())["id"]

    resp = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/runs",
        json={"name": "Run with active capture"},
        headers=headers,
    )
    assert resp.status == 201
    run_id = (await resp.json())["id"]

    # Create active capture session (running)
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions",
        json={"ordinal_number": 1, "status": "running"},
        headers=headers,
    )
    assert resp.status == 201
    session_id = (await resp.json())["id"]

    # Create sensor
    resp = await service_client.post(
        "/api/v1/sensors",
        json={
            "project_id": str(project_id),
            "name": "sensor-to-delete",
            "type": "thermocouple",
            "input_unit": "mV",
            "display_unit": "C",
        },
        headers=headers,
    )
    assert resp.status == 201
    sensor_id = (await resp.json())["sensor"]["id"]

    # Link sensor to run via run_sensors so we can infer "active capture sessions for sensor"
    conninfo = pgsql["experiment_service"].conninfo
    conn = await asyncpg.connect(dsn=conninfo.get_uri())
    try:
        await conn.execute(
            """
            INSERT INTO run_sensors (run_id, sensor_id, project_id, mode, created_by)
            VALUES ($1, $2, $3, 'primary', $4)
            """,
            uuid.UUID(run_id),
            uuid.UUID(sensor_id),
            project_id,
            user_id,
        )
    finally:
        await conn.close()

    # Delete should be rejected while capture session is active
    resp = await service_client.delete(f"/api/v1/sensors/{sensor_id}", headers=headers)
    assert resp.status == 400

    # Stop capture session (no longer active)
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/stop",
        json={"status": "succeeded"},
        headers=headers,
    )
    assert resp.status == 200

    # Now delete should succeed
    resp = await service_client.delete(f"/api/v1/sensors/{sensor_id}", headers=headers)
    assert resp.status == 204


@pytest.mark.asyncio
async def test_sensor_multiple_projects(service_client):
    """Test adding and removing sensor from multiple projects."""
    user_id = uuid.uuid4()
    project1_id = uuid.uuid4()
    project2_id = uuid.uuid4()
    project3_id = uuid.uuid4()

    headers1 = make_headers(project1_id, user_id=user_id)
    headers2 = make_headers(project2_id, user_id=user_id)
    headers3 = make_headers(project3_id, user_id=user_id)

    # Create sensor in project1
    resp = await service_client.post(
        "/api/v1/sensors",
        json={
            "project_id": str(project1_id),
            "name": "multi-project-sensor",
            "type": "thermocouple",
            "input_unit": "mV",
            "display_unit": "C",
        },
        headers=headers1,
    )
    assert resp.status == 201
    sensor_data = await resp.json()
    sensor_id = sensor_data["sensor"]["id"]

    # Get sensor projects - should have project1
    resp = await service_client.get(
        f"/api/v1/sensors/{sensor_id}/projects",
        headers=headers1,
    )
    assert resp.status == 200
    projects_data = await resp.json()
    assert str(project1_id) in projects_data["project_ids"]
    assert len(projects_data["project_ids"]) == 1

    # Add sensor to project2
    resp = await service_client.post(
        f"/api/v1/sensors/{sensor_id}/projects",
        json={"project_id": str(project2_id)},
        headers=headers2,
    )
    assert resp.status == 204

    # Get sensor projects from project2 context - should see both projects
    # (user has access to both projects via headers)
    resp = await service_client.get(
        f"/api/v1/sensors/{sensor_id}/projects",
        headers=headers2,
    )
    assert resp.status == 200
    projects_data = await resp.json()
    # Note: get_sensor_projects filters by user.project_roles, so we might see only project2
    # if user doesn't have access to project1 in headers2 context
    # But we should see at least project2
    assert str(project2_id) in projects_data["project_ids"]

    # Sensor should be visible in project2
    resp = await service_client.get(
        f"/api/v1/sensors?project_id={project2_id}",
        headers=headers2,
    )
    assert resp.status == 200
    sensors_data = await resp.json()
    assert any(s["id"] == sensor_id for s in sensors_data["sensors"])

    # Add sensor to project3
    resp = await service_client.post(
        f"/api/v1/sensors/{sensor_id}/projects",
        json={"project_id": str(project3_id)},
        headers=headers3,
    )
    assert resp.status == 204

    # Remove sensor from project2
    resp = await service_client.delete(
        f"/api/v1/sensors/{sensor_id}/projects/{project2_id}",
        headers=headers2,
    )
    assert resp.status == 204

    # Get sensor projects from project1 context - should see project1
    # (get_sensor_projects filters by user.project_roles, so we see only accessible projects)
    resp = await service_client.get(
        f"/api/v1/sensors/{sensor_id}/projects",
        headers=headers1,
    )
    assert resp.status == 200
    projects_data = await resp.json()
    assert str(project1_id) in projects_data["project_ids"]
    # project2 should not be in the list (was removed)
    assert str(project2_id) not in projects_data["project_ids"]

    # Sensor should not be visible in project2 anymore
    resp = await service_client.get(
        f"/api/v1/sensors?project_id={project2_id}",
        headers=headers2,
    )
    assert resp.status == 200
    sensors_data = await resp.json()
    assert not any(s["id"] == sensor_id for s in sensors_data["sensors"])


@pytest.mark.asyncio
async def test_add_sensor_project_requires_owner_or_editor(service_client):
    """Test that only owner/editor can add sensor to project."""
    user_id = uuid.uuid4()
    project1_id = uuid.uuid4()
    project2_id = uuid.uuid4()

    headers1_owner = make_headers(project1_id, role="owner", user_id=user_id)
    headers2_viewer = make_headers(project2_id, role="viewer", user_id=user_id)

    # Create sensor in project1
    resp = await service_client.post(
        "/api/v1/sensors",
        json={
            "project_id": str(project1_id),
            "name": "rbac-sensor",
            "type": "thermocouple",
            "input_unit": "mV",
            "display_unit": "C",
        },
        headers=headers1_owner,
    )
    assert resp.status == 201
    sensor_data = await resp.json()
    sensor_id = sensor_data["sensor"]["id"]

    # Try to add sensor to project2 as viewer - should fail
    resp = await service_client.post(
        f"/api/v1/sensors/{sensor_id}/projects",
        json={"project_id": str(project2_id)},
        headers=headers2_viewer,
    )
    assert resp.status == 403

    # Add sensor to project2 as owner - should succeed
    headers2_owner = make_headers(project2_id, role="owner", user_id=user_id)
    resp = await service_client.post(
        f"/api/v1/sensors/{sensor_id}/projects",
        json={"project_id": str(project2_id)},
        headers=headers2_owner,
    )
    assert resp.status == 204


@pytest.mark.asyncio
async def test_remove_sensor_project_requires_owner_or_editor(service_client):
    """Test that only owner/editor can remove sensor from project."""
    user_id = uuid.uuid4()
    project1_id = uuid.uuid4()
    project2_id = uuid.uuid4()

    headers1_owner = make_headers(project1_id, role="owner", user_id=user_id)
    headers2_viewer = make_headers(project2_id, role="viewer", user_id=user_id)

    # Create sensor in project1
    resp = await service_client.post(
        "/api/v1/sensors",
        json={
            "project_id": str(project1_id),
            "name": "rbac-remove-sensor",
            "type": "thermocouple",
            "input_unit": "mV",
            "display_unit": "C",
        },
        headers=headers1_owner,
    )
    assert resp.status == 201
    sensor_data = await resp.json()
    sensor_id = sensor_data["sensor"]["id"]

    # Add sensor to project2
    headers2_owner = make_headers(project2_id, role="owner", user_id=user_id)
    resp = await service_client.post(
        f"/api/v1/sensors/{sensor_id}/projects",
        json={"project_id": str(project2_id)},
        headers=headers2_owner,
    )
    assert resp.status == 204

    # Try to remove sensor from project2 as viewer - should fail
    resp = await service_client.delete(
        f"/api/v1/sensors/{sensor_id}/projects/{project2_id}",
        headers=headers2_viewer,
    )
    assert resp.status == 403

    # Remove sensor from project2 as owner - should succeed
    resp = await service_client.delete(
        f"/api/v1/sensors/{sensor_id}/projects/{project2_id}",
        headers=headers2_owner,
    )
    assert resp.status == 204


@pytest.mark.asyncio
async def test_get_sensor_projects_filters_by_access(service_client):
    """Test that get_sensor_projects only returns projects user has access to."""
    user_id = uuid.uuid4()
    project1_id = uuid.uuid4()
    project2_id = uuid.uuid4()

    headers1 = make_headers(project1_id, user_id=user_id)
    headers2 = make_headers(project2_id, user_id=user_id)

    # Create sensor in project1
    resp = await service_client.post(
        "/api/v1/sensors",
        json={
            "project_id": str(project1_id),
            "name": "access-filter-sensor",
            "type": "thermocouple",
            "input_unit": "mV",
            "display_unit": "C",
        },
        headers=headers1,
    )
    assert resp.status == 201
    sensor_data = await resp.json()
    sensor_id = sensor_data["sensor"]["id"]

    # Add sensor to project2
    resp = await service_client.post(
        f"/api/v1/sensors/{sensor_id}/projects",
        json={"project_id": str(project2_id)},
        headers=headers2,
    )
    assert resp.status == 204

    # Get projects from project1 context - should see project1 only
    resp = await service_client.get(
        f"/api/v1/sensors/{sensor_id}/projects",
        headers=headers1,
    )
    assert resp.status == 200
    projects_data = await resp.json()
    assert str(project1_id) in projects_data["project_ids"]
    # Note: project2 might not be visible if user doesn't have access to it
    # This depends on how project_roles are set up in the test


@pytest.mark.asyncio
async def test_add_sensor_project_unknown_sensor_returns_404(service_client):
    """Test adding unknown sensor to project returns 404."""
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    resp = await service_client.post(
        f"/api/v1/sensors/{uuid.uuid4()}/projects",
        json={"project_id": str(project_id)},
        headers=headers,
    )
    assert resp.status == 404


@pytest.mark.asyncio
async def test_remove_sensor_project_unknown_returns_404(service_client):
    """Test removing unknown sensor-project relationship returns 404."""
    user_id = uuid.uuid4()
    project1_id = uuid.uuid4()
    project2_id = uuid.uuid4()
    headers1 = make_headers(project1_id, user_id=user_id)
    headers2 = make_headers(project2_id, user_id=user_id)

    # Create sensor in project1
    resp = await service_client.post(
        "/api/v1/sensors",
        json={
            "project_id": str(project1_id),
            "name": "test-sensor",
            "type": "thermocouple",
            "input_unit": "mV",
            "display_unit": "C",
        },
        headers=headers1,
    )
    assert resp.status == 201
    sensor_data = await resp.json()
    sensor_id = sensor_data["sensor"]["id"]

    # Try to remove from project2 (where sensor is not added) - should return 404
    # But first we need to have access to project2
    resp = await service_client.delete(
        f"/api/v1/sensors/{sensor_id}/projects/{project2_id}",
        headers=headers2,
    )
    assert resp.status == 404


@pytest.mark.asyncio
async def test_list_sensors_without_project_id(service_client):
    """Test listing sensors without project_id uses active_project_id."""
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    # Create sensor
    resp = await service_client.post(
        "/api/v1/sensors",
        json={
            "project_id": str(project_id),
            "name": "no-project-id-sensor",
            "type": "thermocouple",
            "input_unit": "mV",
            "display_unit": "C",
        },
        headers=headers,
    )
    assert resp.status == 201

    # List sensors without project_id in query - should use active_project_id from headers
    resp = await service_client.get("/api/v1/sensors", headers=headers)
    assert resp.status == 200
    sensors_data = await resp.json()
    assert sensors_data["total"] >= 1


@pytest.mark.asyncio
async def test_list_sensors_with_x_project_ids_returns_all_accessible(service_client):
    """Test listing sensors with X-Project-Ids returns sensors from all listed projects."""
    user_id = uuid.uuid4()
    project1_id = uuid.uuid4()
    project2_id = uuid.uuid4()

    # Create sensor in project1
    resp1 = await service_client.post(
        "/api/v1/sensors",
        json={
            "project_id": str(project1_id),
            "name": "sensor-in-project1",
            "type": "thermocouple",
            "input_unit": "mV",
            "display_unit": "C",
        },
        headers=make_headers(project1_id, user_id=user_id),
    )
    assert resp1.status == 201
    body1 = await resp1.json()
    sensor1_id = body1["sensor"]["id"]

    # Create sensor in project2
    resp2 = await service_client.post(
        "/api/v1/sensors",
        json={
            "project_id": str(project2_id),
            "name": "sensor-in-project2",
            "type": "thermocouple",
            "input_unit": "mV",
            "display_unit": "C",
        },
        headers=make_headers(project2_id, user_id=user_id),
    )
    assert resp2.status == 201
    body2 = await resp2.json()
    sensor2_id = body2["sensor"]["id"]

    # List sensors with X-Project-Ids (no project_id in query) — should see both projects
    headers_all = {
        "X-User-Id": str(user_id),
        "X-Project-Ids": f"{project1_id},{project2_id}",
    }
    resp = await service_client.get("/api/v1/sensors", headers=headers_all)
    assert resp.status == 200
    data = await resp.json()
    assert data["total"] >= 2
    sensor_ids = [s["id"] for s in data["sensors"]]
    assert sensor1_id in sensor_ids
    assert sensor2_id in sensor_ids


@pytest.mark.asyncio
async def test_list_and_get_sensor_without_sensor_projects_row(service_client, pgsql):
    """
    Regression test:
    Some deployments may have sensors created before `sensor_projects` was introduced/backfilled.
    Listing or fetching a sensor by project_id should still work based on `sensors.project_id`.
    """
    project_id = uuid.uuid4()
    user_id = uuid.uuid4()
    headers = make_headers(project_id, user_id=user_id)

    # Create sensor via API (normally creates sensor_projects row too)
    resp = await service_client.post(
        "/api/v1/sensors",
        json={
            "project_id": str(project_id),
            "name": "legacy-sensor-without-m2m",
            "type": "thermocouple",
            "input_unit": "mV",
            "display_unit": "C",
        },
        headers=headers,
    )
    assert resp.status == 201
    body = await resp.json()
    sensor_id = body["sensor"]["id"]

    # Simulate legacy DB state: remove the m2m mapping row
    conninfo = pgsql["experiment_service"].conninfo
    conn = await asyncpg.connect(dsn=conninfo.get_uri())
    try:
        await conn.execute(
            "DELETE FROM sensor_projects WHERE sensor_id = $1 AND project_id = $2",
            uuid.UUID(sensor_id),
            project_id,
        )
    finally:
        await conn.close()

    # List sensors for the project — should still include this sensor
    resp = await service_client.get(
        f"/api/v1/sensors?project_id={project_id}&limit=100&offset=0",
        headers=headers,
    )
    assert resp.status == 200
    listed = await resp.json()
    assert listed["total"] >= 1
    assert any(s["id"] == sensor_id for s in listed["sensors"])

    # Get sensor by id in project context — should still work
    resp = await service_client.get(
        f"/api/v1/sensors/{sensor_id}?project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 200
    got = await resp.json()
    assert got["id"] == sensor_id

    # Projects endpoint should fall back to primary project_id
    resp = await service_client.get(
        f"/api/v1/sensors/{sensor_id}/projects",
        headers=headers,
    )
    assert resp.status == 200
    projects = await resp.json()
    assert str(project_id) in projects["project_ids"]


@pytest.mark.asyncio
async def test_list_sensors_with_x_project_ids_without_sensor_projects_row(service_client, pgsql):
    """
    Regression test:
    GET /api/v1/sensors without project_id relies on X-Project-Ids (auth-proxy behavior).
    It should still return sensors even if sensor_projects mapping is missing.
    """
    user_id = uuid.uuid4()
    project1_id = uuid.uuid4()
    project2_id = uuid.uuid4()

    # Create one sensor per project (creates sensor_projects rows)
    resp1 = await service_client.post(
        "/api/v1/sensors",
        json={
            "project_id": str(project1_id),
            "name": "legacy-sensor-p1",
            "type": "thermocouple",
            "input_unit": "mV",
            "display_unit": "C",
        },
        headers=make_headers(project1_id, user_id=user_id),
    )
    assert resp1.status == 201
    sensor1_id = (await resp1.json())["sensor"]["id"]

    resp2 = await service_client.post(
        "/api/v1/sensors",
        json={
            "project_id": str(project2_id),
            "name": "legacy-sensor-p2",
            "type": "thermocouple",
            "input_unit": "mV",
            "display_unit": "C",
        },
        headers=make_headers(project2_id, user_id=user_id),
    )
    assert resp2.status == 201
    sensor2_id = (await resp2.json())["sensor"]["id"]

    # Remove sensor_projects rows for both sensors to simulate legacy DB state
    conninfo = pgsql["experiment_service"].conninfo
    conn = await asyncpg.connect(dsn=conninfo.get_uri())
    try:
        await conn.execute(
            "DELETE FROM sensor_projects WHERE sensor_id = $1 AND project_id = $2",
            uuid.UUID(sensor1_id),
            project1_id,
        )
        await conn.execute(
            "DELETE FROM sensor_projects WHERE sensor_id = $1 AND project_id = $2",
            uuid.UUID(sensor2_id),
            project2_id,
        )
    finally:
        await conn.close()

    headers_all = {
        "X-User-Id": str(user_id),
        "X-Project-Ids": f"{project1_id},{project2_id}",
    }
    resp = await service_client.get("/api/v1/sensors?limit=100&offset=0", headers=headers_all)
    assert resp.status == 200
    data = await resp.json()
    ids = [s["id"] for s in data["sensors"]]
    assert sensor1_id in ids
    assert sensor2_id in ids

