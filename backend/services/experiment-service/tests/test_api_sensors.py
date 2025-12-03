from __future__ import annotations

import uuid

import pytest

from experiment_service.services.idempotency import IDEMPOTENCY_HEADER


def _headers(project_id: uuid.UUID, role: str = "owner") -> dict[str, str]:
    return {
        "X-User-Id": str(uuid.uuid4()),
        "X-Project-Id": str(project_id),
        "X-Project-Role": role,
    }


@pytest.mark.asyncio
async def test_sensor_and_profiles_flow(service_client):
    project_id = uuid.uuid4()
    headers = _headers(project_id)

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

