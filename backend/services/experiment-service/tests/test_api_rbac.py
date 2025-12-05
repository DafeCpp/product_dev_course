from __future__ import annotations

import uuid

import pytest

from tests.utils import make_headers


@pytest.mark.asyncio
async def test_missing_headers_return_401(service_client):
    resp = await service_client.get("/api/v1/experiments")
    assert resp.status == 401


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
async def test_editor_cannot_publish_conversion_profile(service_client):
    project_id = uuid.uuid4()
    owner_headers = make_headers(project_id, role="owner")

    resp = await service_client.post(
        "/api/v1/sensors",
        json={
            "project_id": str(project_id),
            "name": "sensor-rbac",
            "type": "thermocouple",
            "input_unit": "mV",
            "display_unit": "C",
        },
        headers=owner_headers,
    )
    assert resp.status == 201
    sensor_id = (await resp.json())["sensor"]["id"]

    resp_profile = await service_client.post(
        f"/api/v1/sensors/{sensor_id}/conversion-profiles",
        json={
            "version": "v1",
            "kind": "linear",
            "payload": {"a": 1, "b": 2},
        },
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
async def test_viewer_cannot_create_run(service_client):
    project_id = uuid.uuid4()
    owner_headers = make_headers(project_id, role="owner")

    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "RBAC experiment"},
        headers=owner_headers,
    )
    assert resp.status == 201
    experiment_id = (await resp.json())["id"]

    viewer_headers = make_headers(project_id, role="viewer")
    resp_run = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/runs",
        json={"name": "viewer-run"},
        headers=viewer_headers,
    )
    assert resp_run.status == 403


@pytest.mark.asyncio
async def test_viewer_cannot_rotate_sensor_token(service_client):
    project_id = uuid.uuid4()
    owner_headers = make_headers(project_id, role="owner")
    viewer_headers = make_headers(project_id, role="viewer")

    resp = await service_client.post(
        "/api/v1/sensors",
        json={
            "project_id": str(project_id),
            "name": "rbac-sensor",
            "type": "thermocouple",
            "input_unit": "mV",
            "display_unit": "C",
        },
        headers=owner_headers,
    )
    assert resp.status == 201
    sensor_id = (await resp.json())["sensor"]["id"]

    resp_rotate = await service_client.post(
        f"/api/v1/sensors/{sensor_id}/rotate-token",
        headers=viewer_headers,
    )
    assert resp_rotate.status == 403

