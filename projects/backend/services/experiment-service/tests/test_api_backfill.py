"""Integration tests for the backfill API endpoints.

Tests the full lifecycle:
  succeeded → backfill/start → backfilling → backfill/complete → succeeded
"""
from __future__ import annotations

import uuid

import pytest

from tests.utils import make_headers


async def _create_experiment_and_run(service_client, project_id, headers):
    """Helper: create an experiment and a run, return (experiment_id, run_id)."""
    resp = await service_client.post(
        "/api/v1/experiments",
        json={
            "project_id": str(project_id),
            "name": "Backfill Test Experiment",
            "tags": [],
            "metadata": {},
        },
        headers=headers,
    )
    assert resp.status == 201
    experiment = await resp.json()

    resp = await service_client.post(
        f"/api/v1/experiments/{experiment['id']}/runs",
        json={"name": "Backfill Run", "params": {}},
        headers=headers,
    )
    assert resp.status == 201
    run = await resp.json()

    return experiment["id"], run["id"]


async def _create_succeeded_session(service_client, run_id, headers):
    """Helper: create a capture session and stop it as succeeded."""
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions",
        json={"ordinal_number": 1, "status": "running"},
        headers=headers,
    )
    assert resp.status == 201
    session = await resp.json()

    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions/{session['id']}/stop",
        json={"status": "succeeded"},
        headers=headers,
    )
    assert resp.status == 200
    session = await resp.json()
    assert session["status"] == "succeeded"

    return session["id"]


@pytest.mark.asyncio
async def test_backfill_full_lifecycle(service_client):
    """Test: succeeded → start backfill → backfilling → complete → succeeded."""
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    _, run_id = await _create_experiment_and_run(service_client, project_id, headers)
    session_id = await _create_succeeded_session(service_client, run_id, headers)

    # Start backfill
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/backfill/start",
        json={},
        headers=headers,
    )
    assert resp.status == 200
    data = await resp.json()
    assert data["status"] == "backfilling"

    # Complete backfill
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/backfill/complete",
        json={},
        headers=headers,
    )
    assert resp.status == 200
    data = await resp.json()
    assert data["status"] == "succeeded"
    assert "attached_records" in data
    assert isinstance(data["attached_records"], int)


@pytest.mark.asyncio
async def test_backfill_start_requires_succeeded_status(service_client):
    """Starting backfill on a running session should fail."""
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    _, run_id = await _create_experiment_and_run(service_client, project_id, headers)

    # Create a running session (not stopped yet)
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions",
        json={"ordinal_number": 1, "status": "running"},
        headers=headers,
    )
    assert resp.status == 201
    session = await resp.json()
    session_id = session["id"]

    # Try to start backfill on running session — should fail
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/backfill/start",
        json={},
        headers=headers,
    )
    assert resp.status == 400


@pytest.mark.asyncio
async def test_backfill_complete_requires_backfilling_status(service_client):
    """Completing backfill on a succeeded (not backfilling) session should fail."""
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    _, run_id = await _create_experiment_and_run(service_client, project_id, headers)
    session_id = await _create_succeeded_session(service_client, run_id, headers)

    # Try to complete backfill without starting it first — should fail
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/backfill/complete",
        json={},
        headers=headers,
    )
    assert resp.status == 400


@pytest.mark.asyncio
async def test_backfill_start_not_found(service_client):
    """Starting backfill for a non-existent session should return 404."""
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    _, run_id = await _create_experiment_and_run(service_client, project_id, headers)
    fake_session_id = uuid.uuid4()

    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions/{fake_session_id}/backfill/start",
        json={},
        headers=headers,
    )
    assert resp.status == 404


@pytest.mark.asyncio
async def test_backfill_viewer_role_forbidden(service_client):
    """Viewers should not be able to start backfill."""
    project_id = uuid.uuid4()
    owner_headers = make_headers(project_id, role="owner")
    viewer_headers = make_headers(project_id, role="viewer")

    _, run_id = await _create_experiment_and_run(service_client, project_id, owner_headers)
    session_id = await _create_succeeded_session(service_client, run_id, owner_headers)

    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/backfill/start",
        json={},
        headers=viewer_headers,
    )
    assert resp.status == 403


@pytest.mark.asyncio
async def test_backfill_generates_audit_events(service_client):
    """Backfill start and complete should generate audit events."""
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    _, run_id = await _create_experiment_and_run(service_client, project_id, headers)
    session_id = await _create_succeeded_session(service_client, run_id, headers)

    # Start backfill
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/backfill/start",
        json={},
        headers=headers,
    )
    assert resp.status == 200

    # Complete backfill
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/backfill/complete",
        json={},
        headers=headers,
    )
    assert resp.status == 200

    # Check audit events
    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/events",
        headers=headers,
    )
    assert resp.status == 200
    events_data = await resp.json()
    event_types = [e["event_type"] for e in events_data["events"]]
    assert "capture_session.backfill_started" in event_types
    assert "capture_session.backfill_completed" in event_types
