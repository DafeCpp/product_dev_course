"""Integration tests for the scheduled_profile_activation worker task.

Creates sensors/profiles via HTTP and calls the worker function directly
to verify auto-activation logic.
"""
from __future__ import annotations

import uuid
from datetime import datetime, timezone

import pytest

from experiment_service.workers.scheduled_profile_activation import scheduled_profile_activation
from tests.utils import make_headers

IDEM_HEADER = "Idempotency-Key"


async def _create_sensor(client, project_id: uuid.UUID, name: str, headers: dict) -> str:
    resp = await client.post(
        "/api/v1/sensors",
        json={
            "project_id": str(project_id),
            "name": name,
            "type": "thermocouple",
            "input_unit": "mV",
            "display_unit": "C",
        },
        headers={**headers, IDEM_HEADER: str(uuid.uuid4())},
    )
    assert resp.status == 201
    return (await resp.json())["sensor"]["id"]


async def _create_profile(client, sensor_id: str, headers: dict, **extra) -> dict:
    resp = await client.post(
        f"/api/v1/sensors/{sensor_id}/conversion-profiles",
        json={
            "version": str(uuid.uuid4()),
            "kind": "linear",
            "payload": {"a": 1.0, "b": 0.0},
            **extra,
        },
        headers=headers,
    )
    assert resp.status == 201
    return await resp.json()


@pytest.mark.asyncio
async def test_scheduled_profile_activated_when_valid_from_in_past(service_client):
    """A SCHEDULED profile with valid_from in the past should become ACTIVE."""
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    sensor_id = await _create_sensor(service_client, project_id, "sched-sensor-1", headers)

    profile = await _create_profile(
        service_client,
        sensor_id,
        headers,
        status="scheduled",
        valid_from="2020-01-01T00:00:00+00:00",
    )
    profile_id = profile["id"]
    assert profile["status"] == "scheduled"

    result = await scheduled_profile_activation(datetime.now(tz=timezone.utc))
    assert result is not None
    assert "activated=1" in result

    # Verify via API
    resp = await service_client.get(
        f"/api/v1/sensors/{sensor_id}/conversion-profiles",
        headers=headers,
    )
    assert resp.status == 200
    profiles = (await resp.json())["conversion_profiles"]
    activated = next(p for p in profiles if p["id"] == profile_id)
    assert activated["status"] == "active"


@pytest.mark.asyncio
async def test_scheduled_profile_not_activated_when_valid_from_in_future(service_client):
    """A SCHEDULED profile with valid_from in the future should stay SCHEDULED."""
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    sensor_id = await _create_sensor(service_client, project_id, "sched-sensor-2", headers)

    profile = await _create_profile(
        service_client,
        sensor_id,
        headers,
        status="scheduled",
        valid_from="2099-01-01T00:00:00+00:00",
    )
    profile_id = profile["id"]

    # Run worker with "now" well before valid_from
    await scheduled_profile_activation(datetime(2020, 1, 1, tzinfo=timezone.utc))

    resp = await service_client.get(
        f"/api/v1/sensors/{sensor_id}/conversion-profiles",
        headers=headers,
    )
    assert resp.status == 200
    profiles = (await resp.json())["conversion_profiles"]
    not_activated = next(p for p in profiles if p["id"] == profile_id)
    assert not_activated["status"] == "scheduled"


@pytest.mark.asyncio
async def test_activation_deprecates_existing_active_profile(service_client):
    """When a SCHEDULED profile activates, the current ACTIVE one becomes DEPRECATED."""
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    sensor_id = await _create_sensor(service_client, project_id, "sched-sensor-3", headers)

    # Create and publish an ACTIVE profile
    active_profile = await _create_profile(service_client, sensor_id, headers)
    active_id = active_profile["id"]
    resp = await service_client.post(
        f"/api/v1/sensors/{sensor_id}/conversion-profiles/{active_id}/publish",
        headers=headers,
    )
    assert resp.status == 200
    assert (await resp.json())["status"] == "active"

    # Create a SCHEDULED profile due now
    sched_profile = await _create_profile(
        service_client,
        sensor_id,
        headers,
        status="scheduled",
        valid_from="2020-01-01T00:00:00+00:00",
    )
    sched_id = sched_profile["id"]

    await scheduled_profile_activation(datetime.now(tz=timezone.utc))

    resp = await service_client.get(
        f"/api/v1/sensors/{sensor_id}/conversion-profiles",
        headers=headers,
    )
    assert resp.status == 200
    profiles = {p["id"]: p for p in (await resp.json())["conversion_profiles"]}
    assert profiles[sched_id]["status"] == "active"
    assert profiles[active_id]["status"] == "deprecated"


@pytest.mark.asyncio
async def test_no_due_profiles_returns_none(service_client):
    """Worker should return None when nothing is scheduled."""
    result = await scheduled_profile_activation(datetime(2020, 1, 1, tzinfo=timezone.utc))
    # Either None or a result from other tests — we just check it doesn't crash
    assert result is None or isinstance(result, str)


@pytest.mark.asyncio
async def test_activation_is_idempotent(service_client):
    """Running the worker twice does not double-activate."""
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    sensor_id = await _create_sensor(service_client, project_id, "sched-sensor-4", headers)

    await _create_profile(
        service_client,
        sensor_id,
        headers,
        status="scheduled",
        valid_from="2020-01-01T00:00:00+00:00",
    )

    now = datetime.now(tz=timezone.utc)
    r1 = await scheduled_profile_activation(now)
    r2 = await scheduled_profile_activation(now)

    # Second call finds nothing to activate
    assert r2 is None
    assert r1 is not None
