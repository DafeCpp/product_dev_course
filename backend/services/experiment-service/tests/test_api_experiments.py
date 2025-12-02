from __future__ import annotations

import uuid

import pytest


def _headers(project_id: uuid.UUID, role: str = "owner") -> dict[str, str]:
    return {
        "X-User-Id": str(uuid.uuid4()),
        "X-Project-Id": str(project_id),
        "X-Project-Role": role,
    }


@pytest.mark.asyncio
async def test_experiment_run_capture_flow(service_client):
    project_id = uuid.uuid4()
    headers = _headers(project_id)

    # Create experiment
    resp = await service_client.post(
        "/api/v1/experiments",
        json={
            "project_id": str(project_id),
            "name": "Experiment A",
            "description": "test",
            "tags": ["alpha"],
            "metadata": {"priority": "p1"},
        },
        headers=headers,
    )
    assert resp.status == 201
    experiment = await resp.json()
    experiment_id = experiment["id"]

    # List experiments
    resp = await service_client.get("/api/v1/experiments", headers=headers)
    body = await resp.json()
    assert resp.status == 200
    assert body["total"] == 1
    assert body["experiments"][0]["id"] == experiment_id

    # Update experiment
    resp = await service_client.patch(
        f"/api/v1/experiments/{experiment_id}",
        json={"name": "Experiment B"},
        headers=headers,
    )
    assert resp.status == 200
    data = await resp.json()
    assert data["name"] == "Experiment B"

    # Create run
    resp = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/runs",
        json={
            "name": "Run 1",
            "params": {"lr": 0.01},
            "metadata": {"batch": 1},
        },
        headers=headers,
    )
    assert resp.status == 201
    run = await resp.json()
    run_id = run["id"]

    # Update run status
    resp = await service_client.patch(
        f"/api/v1/runs/{run_id}",
        json={"status": "succeeded", "duration_seconds": 42},
        headers=headers,
    )
    assert resp.status == 200
    run = await resp.json()
    assert run["status"] == "succeeded"
    assert run["duration_seconds"] == 42

    # Create capture session
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions",
        json={
            "ordinal_number": 1,
            "status": "running",
            "notes": "session start",
        },
        headers=headers,
    )
    assert resp.status == 201
    capture = await resp.json()
    session_id = capture["id"]

    # Stop capture session
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/stop",
        json={"status": "succeeded"},
        headers=headers,
    )
    assert resp.status == 200
    capture = await resp.json()
    assert capture["status"] == "succeeded"

    # Delete capture session
    resp = await service_client.delete(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}",
        headers=headers,
    )
    assert resp.status == 204

    # Archive experiment
    resp = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/archive",
        headers=headers,
    )
    assert resp.status == 200
    archived = await resp.json()
    assert archived["status"] == "archived"

    # Delete experiment
    resp = await service_client.delete(
        f"/api/v1/experiments/{experiment_id}",
        headers=headers,
    )
    assert resp.status == 204

    resp = await service_client.get(
        f"/api/v1/experiments/{experiment_id}",
        headers=headers,
    )
    assert resp.status == 404


@pytest.mark.asyncio
async def test_batch_update_invalid_run_ids(service_client):
    project_id = uuid.uuid4()
    headers = _headers(project_id)

    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "Experiment C"},
        headers=headers,
    )
    assert resp.status == 201
    experiment_id = (await resp.json())["id"]

    resp = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/runs",
        json={"name": "run"},
        headers=headers,
    )
    assert resp.status == 201

    resp = await service_client.post(
        "/api/v1/runs:batch-status",
        json={"run_ids": [123], "status": "running"},
        headers=headers,
    )
    assert resp.status == 400

