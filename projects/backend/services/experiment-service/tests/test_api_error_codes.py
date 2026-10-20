"""Requests that used to get the wrong status code (LOS-377).

Each test pins the status a realistic bad request gets: a client error
(400/404/409) rather than a 500 from an unhandled exception.
"""
from __future__ import annotations

# pyright: reportMissingImports=false

import uuid

import pytest

from tests.utils import make_headers


async def _create_experiment(service_client, project_id, headers, name="exp"):
    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": name},
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


async def _create_sensor(service_client, project_id, headers, name="sensor"):
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
    assert resp.status == 201
    return (await resp.json())["sensor"]["id"]


async def _start_capture(service_client, run_id, headers):
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions",
        json={"ordinal_number": 1},
        headers=headers,
    )
    assert resp.status == 201
    return (await resp.json())["id"]


# --- unknown enum values in ?status filters -------------------------------


@pytest.mark.asyncio
async def test_unknown_status_filter_is_400(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    experiment_id = await _create_experiment(service_client, project_id, headers)

    for path in (
        "/api/v1/experiments",
        f"/api/v1/experiments/{experiment_id}/runs",
        "/api/v1/sensors",
        "/api/v1/experiments/export",
        f"/api/v1/experiments/{experiment_id}/runs/export",
    ):
        resp = await service_client.get(
            path, params={"project_id": str(project_id), "status": "bogus"}, headers=headers
        )
        assert resp.status == 400, path
        assert "status" in await resp.text()


# --- empty PATCH bodies -----------------------------------------------------


@pytest.mark.asyncio
async def test_empty_patch_is_400(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    experiment_id = await _create_experiment(service_client, project_id, headers)
    run_id = await _create_run(service_client, experiment_id, headers)
    sensor_id = await _create_sensor(service_client, project_id, headers)

    for path in (
        f"/api/v1/experiments/{experiment_id}",
        f"/api/v1/runs/{run_id}",
        f"/api/v1/sensors/{sensor_id}",
    ):
        resp = await service_client.patch(path, json={}, headers=headers)
        assert resp.status == 400, path
        assert (await resp.json())["error"] == "No fields provided for update"


@pytest.mark.asyncio
async def test_empty_patch_of_missing_experiment_is_404(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    resp = await service_client.patch(
        f"/api/v1/experiments/{uuid.uuid4()}", json={}, headers=headers
    )
    assert resp.status == 404


# --- renames onto an existing name -----------------------------------------


@pytest.mark.asyncio
async def test_rename_experiment_to_existing_name_is_409(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    await _create_experiment(service_client, project_id, headers, name="alpha")
    beta_id = await _create_experiment(service_client, project_id, headers, name="beta")

    resp = await service_client.patch(
        f"/api/v1/experiments/{beta_id}", json={"name": "Alpha"}, headers=headers
    )
    assert resp.status == 409
    assert "already exists" in (await resp.json())["error"]


@pytest.mark.asyncio
async def test_rename_sensor_to_existing_name_is_409(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    await _create_sensor(service_client, project_id, headers, name="alpha")
    beta_id = await _create_sensor(service_client, project_id, headers, name="beta")

    resp = await service_client.patch(
        f"/api/v1/sensors/{beta_id}", json={"name": "ALPHA"}, headers=headers
    )
    assert resp.status == 409
    assert "already exists" in (await resp.json())["error"]


@pytest.mark.asyncio
async def test_sensor_unknown_active_profile_is_404(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    sensor_id = await _create_sensor(service_client, project_id, headers)

    resp = await service_client.patch(
        f"/api/v1/sensors/{sensor_id}",
        json={"active_profile_id": str(uuid.uuid4())},
        headers=headers,
    )
    assert resp.status == 404


# --- conversion profiles ----------------------------------------------------


@pytest.mark.asyncio
async def test_duplicate_conversion_profile_version_is_409(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    sensor_id = await _create_sensor(service_client, project_id, headers)
    profile = {"version": "v1", "kind": "linear", "payload": {"a": 1.0, "b": 0.0}}

    resp = await service_client.post(
        f"/api/v1/sensors/{sensor_id}/conversion-profiles", json=profile, headers=headers
    )
    assert resp.status == 201
    resp = await service_client.post(
        f"/api/v1/sensors/{sensor_id}/conversion-profiles", json=profile, headers=headers
    )
    assert resp.status == 409
    assert "v1" in (await resp.json())["error"]


# --- run sensors ------------------------------------------------------------


@pytest.mark.asyncio
async def test_attach_unknown_sensor_or_run_is_404(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    experiment_id = await _create_experiment(service_client, project_id, headers)
    run_id = await _create_run(service_client, experiment_id, headers)
    sensor_id = await _create_sensor(service_client, project_id, headers)

    # Sensor of another project.
    other_project = uuid.uuid4()
    other_sensor = await _create_sensor(service_client, other_project, make_headers(other_project))

    for path in (
        f"/api/v1/runs/{run_id}/sensors/{uuid.uuid4()}",
        f"/api/v1/runs/{run_id}/sensors/{other_sensor}",
        f"/api/v1/runs/{uuid.uuid4()}/sensors/{sensor_id}",
    ):
        resp = await service_client.post(path, json={}, headers=headers)
        assert resp.status == 404, path

    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/sensors/{sensor_id}", json={}, headers=headers
    )
    assert resp.status == 201


# --- capture sessions -------------------------------------------------------


@pytest.mark.asyncio
async def test_second_active_capture_session_is_409(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    experiment_id = await _create_experiment(service_client, project_id, headers)
    run_a = await _create_run(service_client, experiment_id, headers, status="running")
    run_b = await _create_run(service_client, experiment_id, headers, status="running")
    await _start_capture(service_client, run_a, headers)

    resp = await service_client.post(
        f"/api/v1/runs/{run_b}/capture-sessions",
        json={"ordinal_number": 1},
        headers=headers,
    )
    assert resp.status == 409
    assert "Active capture session" in (await resp.json())["error"]


@pytest.mark.asyncio
async def test_stop_capture_session_of_another_run_is_404(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    experiment_id = await _create_experiment(service_client, project_id, headers)
    run_a = await _create_run(service_client, experiment_id, headers, status="running")
    run_b = await _create_run(service_client, experiment_id, headers, status="running")
    session_id = await _start_capture(service_client, run_a, headers)

    resp = await service_client.post(
        f"/api/v1/runs/{run_b}/capture-sessions/{session_id}/stop",
        json={},
        headers=headers,
    )
    assert resp.status == 404

    resp = await service_client.get(
        f"/api/v1/runs/{run_a}/capture-sessions", headers=headers
    )
    assert (await resp.json())["capture_sessions"][0]["status"] == "running"


@pytest.mark.asyncio
async def test_stop_capture_session_bad_stopped_at_is_400(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    experiment_id = await _create_experiment(service_client, project_id, headers)
    run_id = await _create_run(service_client, experiment_id, headers, status="running")
    session_id = await _start_capture(service_client, run_id, headers)

    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/stop",
        json={"stopped_at": "yesterday"},
        headers=headers,
    )
    assert resp.status == 400


# --- body field types -------------------------------------------------------


@pytest.mark.asyncio
async def test_non_string_project_id_in_body_is_400(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    sensor_id = await _create_sensor(service_client, project_id, headers)

    resp = await service_client.post(
        "/api/v1/experiments", json={"project_id": 123, "name": "x"}, headers=headers
    )
    assert resp.status == 400
    resp = await service_client.post(
        f"/api/v1/sensors/{sensor_id}/projects", json={"project_id": 123}, headers=headers
    )
    assert resp.status == 400


@pytest.mark.asyncio
async def test_compare_non_integer_step_is_400(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    experiment_id = await _create_experiment(service_client, project_id, headers)
    run_a = await _create_run(service_client, experiment_id, headers)
    run_b = await _create_run(service_client, experiment_id, headers)

    resp = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/compare",
        json={"run_ids": [run_a, run_b], "metric_names": ["loss"], "from_step": "abc"},
        headers=headers,
    )
    assert resp.status == 400
    assert "from_step" in await resp.text()


# --- validation errors keep their message ----------------------------------


@pytest.mark.asyncio
async def test_compare_validation_error_names_the_problem(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    experiment_id = await _create_experiment(service_client, project_id, headers)
    run_id = await _create_run(service_client, experiment_id, headers)

    resp = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/compare",
        json={"run_ids": [run_id], "metric_names": ["loss"]},
        headers=headers,
    )
    assert resp.status == 400
    assert "at least 2" in await resp.text()

    resp = await service_client.get(
        f"/api/v1/experiments/{experiment_id}/compare",
        params={"run_ids": run_id, "names": "loss"},
        headers=headers,
    )
    assert resp.status == 400
    assert "at least 2" in await resp.text()


@pytest.mark.asyncio
async def test_metrics_batch_too_large_names_the_limit(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    experiment_id = await _create_experiment(service_client, project_id, headers)
    run_id = await _create_run(service_client, experiment_id, headers)
    point = {"name": "loss", "step": 1, "value": 0.5, "timestamp": "2026-01-01T00:00:00Z"}

    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/metrics",
        json={"metrics": [point] * 10_001},
        headers=headers,
    )
    assert resp.status == 400
    assert "Batch too large" in await resp.text()
