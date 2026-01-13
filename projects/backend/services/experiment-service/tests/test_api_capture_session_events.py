from __future__ import annotations

import uuid

import pytest

from tests.utils import make_headers


@pytest.mark.asyncio
async def test_capture_session_events_recorded_and_readable(service_client):
    project_id = uuid.uuid4()
    actor_id = uuid.uuid4()

    headers_owner = make_headers(project_id, user_id=actor_id, role="owner")
    headers_viewer = make_headers(project_id, role="viewer")

    # Create experiment
    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "Audit experiment"},
        headers=headers_owner,
    )
    assert resp.status == 201
    experiment_id = (await resp.json())["id"]

    # Create run
    resp = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/runs",
        json={"name": "Audit run"},
        headers=headers_owner,
    )
    assert resp.status == 201
    run_id = (await resp.json())["id"]

    # Create capture session
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions",
        json={"ordinal_number": 1, "status": "running", "notes": "start"},
        headers=headers_owner,
    )
    assert resp.status == 201
    session_id = (await resp.json())["id"]

    # Stop capture session
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/stop",
        json={"status": "succeeded", "notes": "done"},
        headers=headers_owner,
    )
    assert resp.status == 200

    # Viewer can read audit events
    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/events",
        headers=headers_viewer,
    )
    assert resp.status == 200
    body = await resp.json()
    assert body["total"] == 2
    assert [evt["event_type"] for evt in body["events"]] == [
        "capture_session.created",
        "capture_session.stopped",
    ]
    assert body["events"][0]["actor_id"] == str(actor_id)
    assert body["events"][0]["actor_role"] == "owner"

