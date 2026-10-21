"""Status conflicts answer 409 with a JSON reason (LOS-387).

An invalid status transition, or an action refused because a related entity
is still active, is a conflict with the resource's current state: the client
gets ``409 {"error": "<reason>"}`` instead of a bare ``400 Bad request``.
A create request asking for a status a new entity cannot start in stays a
400, but now names the problem too.
"""
from __future__ import annotations

# pyright: reportMissingImports=false

import uuid

import pytest

from tests.utils import make_headers


async def _create_experiment(service_client, project_id, headers):
    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": f"exp-{uuid.uuid4()}"},
        headers=headers,
    )
    assert resp.status == 201
    return (await resp.json())["id"]


async def _create_run(service_client, experiment_id, headers, status=None):
    body: dict = {"name": "run"}
    if status is not None:
        body["status"] = status
    resp = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/runs", json=body, headers=headers
    )
    assert resp.status == 201
    return (await resp.json())["id"]


async def _start_capture(service_client, run_id, headers, status="running"):
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions",
        json={"ordinal_number": 1, "status": status},
        headers=headers,
    )
    assert resp.status == 201
    return (await resp.json())["id"]


async def _get_run_status(service_client, run_id, headers):
    resp = await service_client.get(f"/api/v1/runs/{run_id}", headers=headers)
    assert resp.status == 200
    return (await resp.json())["status"]


async def _error(resp) -> str:
    assert resp.content_type == "application/json"
    return (await resp.json())["error"]


# --- invalid status transitions ---------------------------------------------


@pytest.mark.asyncio
async def test_run_invalid_transition_is_409(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    experiment_id = await _create_experiment(service_client, project_id, headers)
    run_id = await _create_run(service_client, experiment_id, headers)

    resp = await service_client.patch(
        f"/api/v1/runs/{run_id}", json={"status": "succeeded"}, headers=headers
    )
    assert resp.status == 409
    assert await _error(resp) == "Invalid run status transition: draft → succeeded"
    assert await _get_run_status(service_client, run_id, headers) == "draft"


@pytest.mark.asyncio
async def test_archive_running_experiment_is_409(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    experiment_id = await _create_experiment(service_client, project_id, headers)
    resp = await service_client.patch(
        f"/api/v1/experiments/{experiment_id}", json={"status": "running"}, headers=headers
    )
    assert resp.status == 200

    resp = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/archive", headers=headers
    )
    assert resp.status == 409
    assert "running → archived" in await _error(resp)


@pytest.mark.asyncio
async def test_stop_draft_capture_session_is_409(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    experiment_id = await _create_experiment(service_client, project_id, headers)
    run_id = await _create_run(service_client, experiment_id, headers, status="running")
    session_id = await _start_capture(service_client, run_id, headers, status="draft")

    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/stop",
        json={"status": "succeeded"},
        headers=headers,
    )
    assert resp.status == 409
    assert "draft → succeeded" in await _error(resp)


@pytest.mark.asyncio
async def test_publish_deprecated_conversion_profile_is_409(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    resp = await service_client.post(
        "/api/v1/sensors",
        json={
            "project_id": str(project_id),
            "name": "sensor",
            "type": "thermocouple",
            "input_unit": "mV",
            "display_unit": "C",
        },
        headers=headers,
    )
    assert resp.status == 201
    sensor_id = (await resp.json())["sensor"]["id"]
    resp = await service_client.post(
        f"/api/v1/sensors/{sensor_id}/conversion-profiles",
        json={
            "version": "v1",
            "kind": "linear",
            "payload": {"a": 1.0, "b": 0.0},
            "status": "deprecated",
        },
        headers=headers,
    )
    assert resp.status == 201
    profile_id = (await resp.json())["id"]

    resp = await service_client.post(
        f"/api/v1/sensors/{sensor_id}/conversion-profiles/{profile_id}/publish",
        headers=headers,
    )
    assert resp.status == 409
    assert "deprecated → active" in await _error(resp)


# --- refusals blocked by an active entity ------------------------------------


@pytest.mark.asyncio
async def test_archive_run_with_active_capture_session_is_409(service_client):
    """A draft run may be archived, but not while one of its sessions records."""
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    experiment_id = await _create_experiment(service_client, project_id, headers)
    run_id = await _create_run(service_client, experiment_id, headers)
    await _start_capture(service_client, run_id, headers)

    resp = await service_client.patch(
        f"/api/v1/runs/{run_id}", json={"status": "archived"}, headers=headers
    )
    assert resp.status == 409
    assert await _error(resp) == "Cannot archive run while it has an active capture session"


@pytest.mark.asyncio
async def test_delete_run_with_active_capture_session_is_409(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    experiment_id = await _create_experiment(service_client, project_id, headers)
    run_id = await _create_run(service_client, experiment_id, headers, status="running")
    await _start_capture(service_client, run_id, headers)

    resp = await service_client.delete(f"/api/v1/runs/{run_id}", headers=headers)
    assert resp.status == 409
    assert await _error(resp) == "Cannot delete run while it has an active capture session"


# --- bulk status: all-or-nothing, the 409 names the refused run --------------


@pytest.mark.asyncio
async def test_batch_status_invalid_transition_is_409_and_changes_nothing(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    experiment_id = await _create_experiment(service_client, project_id, headers)
    ok_run = await _create_run(service_client, experiment_id, headers, status="running")
    bad_run = await _create_run(service_client, experiment_id, headers)

    resp = await service_client.post(
        "/api/v1/runs:batch-status",
        json={"run_ids": [ok_run, bad_run], "status": "succeeded"},
        headers=headers,
    )
    assert resp.status == 409
    error = await _error(resp)
    assert bad_run in error
    assert "draft → succeeded" in error
    assert await _get_run_status(service_client, ok_run, headers) == "running"
    assert await _get_run_status(service_client, bad_run, headers) == "draft"


@pytest.mark.asyncio
async def test_batch_status_finish_with_active_capture_session_is_409(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    experiment_id = await _create_experiment(service_client, project_id, headers)
    run_id = await _create_run(service_client, experiment_id, headers, status="running")
    await _start_capture(service_client, run_id, headers)

    resp = await service_client.post(
        "/api/v1/runs:batch-status",
        json={"run_ids": [run_id], "status": "failed"},
        headers=headers,
    )
    assert resp.status == 409
    error = await _error(resp)
    assert run_id in error
    assert "Cannot finish run while it has an active capture session" in error
    assert await _get_run_status(service_client, run_id, headers) == "running"


# --- create with an impossible initial status stays 400 ----------------------


@pytest.mark.asyncio
async def test_create_with_invalid_initial_status_is_400_with_reason(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "exp", "status": "succeeded"},
        headers=headers,
    )
    assert resp.status == 400
    assert await _error(resp) == "Experiment cannot be created in 'succeeded' status"

    experiment_id = await _create_experiment(service_client, project_id, headers)
    resp = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/runs",
        json={"name": "run", "status": "failed"},
        headers=headers,
    )
    assert resp.status == 400
    assert await _error(resp) == "Run cannot be created in 'failed' status"

    run_id = await _create_run(service_client, experiment_id, headers, status="running")
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions",
        json={"ordinal_number": 1, "status": "succeeded"},
        headers=headers,
    )
    assert resp.status == 400
    assert await _error(resp) == "Capture session cannot be created in 'succeeded' status"
