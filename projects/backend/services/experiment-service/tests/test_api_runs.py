from __future__ import annotations

import uuid

import pytest

from tests.utils import make_headers


@pytest.mark.asyncio
async def test_runs_bulk_tags_add_remove_and_set(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    # Create experiment
    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "Experiment Tags"},
        headers=headers,
    )
    assert resp.status == 201
    experiment_id = (await resp.json())["id"]

    # Create run
    resp = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/runs",
        json={"name": "Run tags"},
        headers=headers,
    )
    assert resp.status == 201
    run_id = (await resp.json())["id"]

    # Add tags (dedup + strip + ignore empty)
    resp = await service_client.post(
        "/api/v1/runs:bulk-tags",
        json={"run_ids": [run_id], "add_tags": [" alpha ", "beta", "", "alpha"]},
        headers=headers,
    )
    assert resp.status == 200
    body = await resp.json()
    assert body["runs"][0]["id"] == run_id
    assert set(body["runs"][0]["tags"]) == {"alpha", "beta"}

    # Remove one tag
    resp = await service_client.post(
        "/api/v1/runs:bulk-tags",
        json={"run_ids": [run_id], "remove_tags": ["beta"]},
        headers=headers,
    )
    assert resp.status == 200
    body = await resp.json()
    assert body["runs"][0]["tags"] == ["alpha"]

    # Set tags (can clear by empty list)
    resp = await service_client.post(
        "/api/v1/runs:bulk-tags",
        json={"run_ids": [run_id], "set_tags": ["x", "y"]},
        headers=headers,
    )
    assert resp.status == 200
    body = await resp.json()
    assert set(body["runs"][0]["tags"]) == {"x", "y"}

    # Verify GET run returns tags too
    resp = await service_client.get(f"/api/v1/runs/{run_id}", headers=headers)
    assert resp.status == 200
    run = await resp.json()
    assert set(run["tags"]) == {"x", "y"}


@pytest.mark.asyncio
async def test_runs_bulk_tags_validation(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    # empty run_ids
    resp = await service_client.post(
        "/api/v1/runs:bulk-tags",
        json={"run_ids": [], "add_tags": ["a"]},
        headers=headers,
    )
    assert resp.status == 400

    # set_tags cannot be combined with add/remove
    resp = await service_client.post(
        "/api/v1/runs:bulk-tags",
        json={"run_ids": [str(uuid.uuid4())], "set_tags": ["a"], "add_tags": ["b"]},
        headers=headers,
    )
    assert resp.status == 400

    # must specify add or remove if not using set_tags
    resp = await service_client.post(
        "/api/v1/runs:bulk-tags",
        json={"run_ids": [str(uuid.uuid4())]},
        headers=headers,
    )
    assert resp.status == 400


@pytest.mark.asyncio
async def test_runs_bulk_tags_requires_owner_or_editor(service_client):
    project_id = uuid.uuid4()
    headers_viewer = make_headers(project_id, role="viewer")

    resp = await service_client.post(
        "/api/v1/runs:bulk-tags",
        json={"run_ids": [str(uuid.uuid4())], "add_tags": ["a"]},
        headers=headers_viewer,
    )
    assert resp.status in (403, 404)


@pytest.mark.asyncio
async def test_run_cannot_finish_with_active_capture_sessions(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    # Create experiment
    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "Experiment Invariants"},
        headers=headers,
    )
    assert resp.status == 201
    experiment_id = (await resp.json())["id"]

    # Create run (draft)
    resp = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/runs",
        json={"name": "Run with capture"},
        headers=headers,
    )
    assert resp.status == 201
    run_id = (await resp.json())["id"]

    # Move run to running (draft -> running is allowed)
    resp = await service_client.patch(
        f"/api/v1/runs/{run_id}",
        json={"status": "running"},
        headers=headers,
    )
    assert resp.status == 200

    # Create active capture session
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions",
        json={"ordinal_number": 1, "status": "running"},
        headers=headers,
    )
    assert resp.status == 201
    session_id = (await resp.json())["id"]

    # Try to finish run while capture session is active -> 409
    resp = await service_client.patch(
        f"/api/v1/runs/{run_id}",
        json={"status": "succeeded"},
        headers=headers,
    )
    assert resp.status == 409
    assert (await resp.json())["error"] == (
        "Cannot finish run while it has an active capture session"
    )

    # Stop capture session -> succeeded
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/stop",
        json={"status": "succeeded"},
        headers=headers,
    )
    assert resp.status == 200

    # Now finishing run should work
    resp = await service_client.patch(
        f"/api/v1/runs/{run_id}",
        json={"status": "succeeded"},
        headers=headers,
    )
    assert resp.status == 200

