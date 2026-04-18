from __future__ import annotations

# pyright: reportMissingImports=false

import uuid

import pytest

from experiment_service.services.idempotency import IDEMPOTENCY_HEADER
from tests.utils import make_headers


@pytest.mark.asyncio
async def test_experiment_run_capture_flow(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

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

    # Update run status to running
    resp = await service_client.patch(
        f"/api/v1/runs/{run_id}",
        json={"status": "running"},
        headers=headers,
    )
    assert resp.status == 200

    # Create capture session (run is running — allowed)
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

    # Complete run after capture session is done
    resp = await service_client.patch(
        f"/api/v1/runs/{run_id}",
        json={"status": "succeeded", "duration_seconds": 42},
        headers=headers,
    )
    assert resp.status == 200
    run = await resp.json()
    assert run["status"] == "succeeded"
    assert run["duration_seconds"] == 42

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
async def test_capture_session_cannot_be_deleted_while_active(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "Experiment Active Delete"},
        headers=headers,
    )
    assert resp.status == 201
    experiment_id = (await resp.json())["id"]

    resp = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/runs",
        json={"name": "Run for active session"},
        headers=headers,
    )
    assert resp.status == 201
    run_id = (await resp.json())["id"]

    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions",
        json={"ordinal_number": 1, "status": "running"},
        headers=headers,
    )
    assert resp.status == 201
    session_id = (await resp.json())["id"]

    # Cannot delete while running
    resp = await service_client.delete(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}",
        headers=headers,
    )
    assert resp.status == 400

    # Stop (becomes final) then delete should work
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/stop",
        json={"status": "succeeded"},
        headers=headers,
    )
    assert resp.status == 200

    resp = await service_client.delete(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}",
        headers=headers,
    )
    assert resp.status == 204


@pytest.mark.asyncio
async def test_batch_update_invalid_run_ids(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

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


@pytest.mark.asyncio
async def test_capture_session_requires_ordinal_number(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "Experiment D"},
        headers=headers,
    )
    assert resp.status == 201
    experiment_id = (await resp.json())["id"]

    resp_run = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/runs",
        json={"name": "run-without-ordinal"},
        headers=headers,
    )
    assert resp_run.status == 201
    run_id = (await resp_run.json())["id"]

    resp_session = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions",
        json={"status": "running"},
        headers=headers,
    )
    assert resp_session.status == 400
    body = await resp_session.text()
    assert "ordinal_number" in body


@pytest.mark.asyncio
async def test_create_experiment_idempotency(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    idem_headers = {**headers, IDEMPOTENCY_HEADER: "idem-key-1"}

    payload = {"project_id": str(project_id), "name": "Idempotent Experiment"}

    resp1 = await service_client.post("/api/v1/experiments", json=payload, headers=idem_headers)
    assert resp1.status == 201
    first = await resp1.json()

    resp2 = await service_client.post("/api/v1/experiments", json=payload, headers=idem_headers)
    assert resp2.status == 201
    second = await resp2.json()
    assert first == second

    # Different payload with same key should yield 409
    resp3 = await service_client.post(
        "/api/v1/experiments",
        json={**payload, "description": "changed"},
        headers=idem_headers,
    )
    assert resp3.status == 409


@pytest.mark.asyncio
async def test_update_experiment_invalid_status_transition(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "Bad Transition"},
        headers=headers,
    )
    assert resp.status == 201
    experiment_id = (await resp.json())["id"]

    resp = await service_client.patch(
        f"/api/v1/experiments/{experiment_id}",
        json={"status": "succeeded"},
        headers=headers,
    )
    assert resp.status == 400
    body = await resp.text()
    assert "Invalid experiment status transition" in body


@pytest.mark.asyncio
async def test_delete_experiment_not_found_returns_404(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    missing_id = uuid.uuid4()

    resp = await service_client.delete(
        f"/api/v1/experiments/{missing_id}",
        headers=headers,
    )
    assert resp.status == 404


@pytest.mark.asyncio
async def test_batch_update_status_rejects_invalid_status_value(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    resp = await service_client.post(
        "/api/v1/runs:batch-status",
        json={
            "run_ids": [str(uuid.uuid4())],
            "status": "not-a-status",
        },
        headers=headers,
    )
    assert resp.status == 400


# ---------------------------------------------------------------------------
# Domain invariant tests
# ---------------------------------------------------------------------------


async def _create_experiment_and_run(service_client, project_id, *, run_status="draft"):
    """Helper: create experiment + run, optionally transition run to a given status."""
    headers = make_headers(project_id)
    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": f"Exp {uuid.uuid4()}"},
        headers=headers,
    )
    assert resp.status == 201
    experiment_id = (await resp.json())["id"]

    resp = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/runs",
        json={"name": "Test Run"},
        headers=headers,
    )
    assert resp.status == 201
    run_id = (await resp.json())["id"]

    if run_status == "running":
        resp = await service_client.patch(
            f"/api/v1/runs/{run_id}",
            json={"status": "running"},
            headers=headers,
        )
        assert resp.status == 200

    return experiment_id, run_id, headers


@pytest.mark.asyncio
async def test_delete_run_blocked_while_active_capture_session(service_client):
    """Cannot delete a run while it has an active (running) capture session."""
    project_id = uuid.uuid4()
    _, run_id, headers = await _create_experiment_and_run(
        service_client, project_id, run_status="running"
    )

    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions",
        json={"ordinal_number": 1, "status": "running"},
        headers=headers,
    )
    assert resp.status == 201

    resp = await service_client.delete(f"/api/v1/runs/{run_id}", headers=headers)
    assert resp.status == 400
    body = await resp.text()
    assert "active" in body.lower() or "capture session" in body.lower()


@pytest.mark.asyncio
async def test_delete_run_allowed_after_capture_session_stopped(service_client):
    """Deleting a run is allowed once all capture sessions are stopped."""
    project_id = uuid.uuid4()
    _, run_id, headers = await _create_experiment_and_run(
        service_client, project_id, run_status="running"
    )

    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions",
        json={"ordinal_number": 1, "status": "running"},
        headers=headers,
    )
    assert resp.status == 201
    session_id = (await resp.json())["id"]

    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/stop",
        json={"status": "succeeded"},
        headers=headers,
    )
    assert resp.status == 200

    resp = await service_client.delete(f"/api/v1/runs/{run_id}", headers=headers)
    assert resp.status == 204


@pytest.mark.asyncio
async def test_delete_experiment_blocked_while_run_is_running(service_client):
    """Cannot delete an experiment while it has a run in 'running' status."""
    project_id = uuid.uuid4()
    experiment_id, _, headers = await _create_experiment_and_run(
        service_client, project_id, run_status="running"
    )

    resp = await service_client.delete(
        f"/api/v1/experiments/{experiment_id}", headers=headers
    )
    assert resp.status == 400
    body = await resp.text()
    assert "running" in body.lower()


@pytest.mark.asyncio
async def test_delete_experiment_allowed_when_no_running_runs(service_client):
    """Deleting an experiment with only draft/succeeded runs is allowed."""
    project_id = uuid.uuid4()
    experiment_id, _, headers = await _create_experiment_and_run(
        service_client, project_id, run_status="draft"
    )

    resp = await service_client.delete(
        f"/api/v1/experiments/{experiment_id}", headers=headers
    )
    assert resp.status == 204


@pytest.mark.asyncio
async def test_create_capture_session_blocked_for_succeeded_run(service_client):
    """Cannot create a capture session for a run in 'succeeded' status."""
    project_id = uuid.uuid4()
    _, run_id, headers = await _create_experiment_and_run(
        service_client, project_id, run_status="running"
    )

    resp = await service_client.patch(
        f"/api/v1/runs/{run_id}",
        json={"status": "succeeded"},
        headers=headers,
    )
    assert resp.status == 200

    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions",
        json={"ordinal_number": 1, "status": "running"},
        headers=headers,
    )
    assert resp.status == 400
    body = await resp.text()
    assert "succeeded" in body.lower()


@pytest.mark.asyncio
async def test_create_capture_session_blocked_for_failed_run(service_client):
    """Cannot create a capture session for a run in 'failed' status."""
    project_id = uuid.uuid4()
    _, run_id, headers = await _create_experiment_and_run(
        service_client, project_id, run_status="running"
    )

    resp = await service_client.patch(
        f"/api/v1/runs/{run_id}",
        json={"status": "failed"},
        headers=headers,
    )
    assert resp.status == 200

    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions",
        json={"ordinal_number": 1, "status": "running"},
        headers=headers,
    )
    assert resp.status == 400
    body = await resp.text()
    assert "failed" in body.lower()


@pytest.mark.asyncio
async def test_create_capture_session_allowed_for_draft_and_running_run(service_client):
    """Can create a capture session for a run in 'draft' or 'running' status."""
    project_id = uuid.uuid4()

    # draft run
    _, run_id_draft, headers = await _create_experiment_and_run(
        service_client, project_id, run_status="draft"
    )
    resp = await service_client.post(
        f"/api/v1/runs/{run_id_draft}/capture-sessions",
        json={"ordinal_number": 1, "status": "draft"},
        headers=headers,
    )
    assert resp.status == 201

    # Stop the draft session so it doesn't block the next one
    session_id = (await resp.json())["id"]
    await service_client.post(
        f"/api/v1/runs/{run_id_draft}/capture-sessions/{session_id}/stop",
        json={"status": "succeeded"},
        headers=headers,
    )

    # running run (different project to avoid "active session" collision)
    project_id2 = uuid.uuid4()
    _, run_id_running, headers2 = await _create_experiment_and_run(
        service_client, project_id2, run_status="running"
    )
    resp = await service_client.post(
        f"/api/v1/runs/{run_id_running}/capture-sessions",
        json={"ordinal_number": 1, "status": "running"},
        headers=headers2,
    )
    assert resp.status == 201
