"""Attaching a sensor to another project goes through an approved request (LOS-374)."""
from __future__ import annotations

# pyright: reportMissingImports=false

import uuid

import pytest

from tests.utils import make_headers


async def _create_sensor(service_client, project_id, headers, name="shared-sensor"):
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


async def _request(service_client, sensor_id, project_id, headers):
    return await service_client.post(
        f"/api/v1/sensors/{sensor_id}/projects",
        json={"project_id": str(project_id)},
        headers=headers,
    )


async def _sensor_ids(service_client, project_id, headers):
    resp = await service_client.get(f"/api/v1/sensors?project_id={project_id}", headers=headers)
    assert resp.status == 200
    return {s["id"] for s in (await resp.json())["sensors"]}


async def _setup(service_client):
    owner_project, target_project = uuid.uuid4(), uuid.uuid4()
    owner_headers = make_headers(owner_project)
    target_headers = make_headers(target_project)
    sensor_id = await _create_sensor(service_client, owner_project, owner_headers)
    return owner_project, owner_headers, target_project, target_headers, sensor_id


@pytest.mark.asyncio
async def test_request_does_not_attach_until_approved(service_client):
    owner_project, owner_headers, target_project, target_headers, sensor_id = await _setup(
        service_client
    )

    resp = await _request(service_client, sensor_id, target_project, target_headers)
    assert resp.status == 202
    item = await resp.json()
    assert item["status"] == "pending"
    assert item["sensor_id"] == sensor_id
    assert item["project_id"] == str(target_project)

    # Not attached yet: the target project cannot see the sensor.
    assert sensor_id not in await _sensor_ids(service_client, target_project, target_headers)
    resp = await service_client.get(
        f"/api/v1/sensors/{sensor_id}?project_id={target_project}", headers=target_headers
    )
    assert resp.status == 404

    # Repeating the request returns the same open request.
    resp = await _request(service_client, sensor_id, target_project, target_headers)
    assert resp.status == 202
    assert (await resp.json())["id"] == item["id"]

    # The owner project sees it as incoming, with the sensor name.
    resp = await service_client.get(
        f"/api/v1/sensor-project-requests?project_id={owner_project}&direction=incoming&status=pending",
        headers=owner_headers,
    )
    assert resp.status == 200
    incoming = (await resp.json())["requests"]
    assert [r["id"] for r in incoming] == [item["id"]]
    assert incoming[0]["sensor_name"] == "shared-sensor"

    # The target project sees it as outgoing, without the sensor name.
    resp = await service_client.get(
        f"/api/v1/sensor-project-requests?project_id={target_project}&direction=outgoing",
        headers=target_headers,
    )
    outgoing = (await resp.json())["requests"]
    assert [r["id"] for r in outgoing] == [item["id"]]
    assert outgoing[0]["sensor_name"] is None

    resp = await service_client.post(
        f"/api/v1/sensor-project-requests/{item['id']}/approve?project_id={owner_project}",
        headers=owner_headers,
    )
    assert resp.status == 200
    approved = await resp.json()
    assert approved["status"] == "approved"
    assert approved["decided_in_project_id"] == str(owner_project)
    assert approved["decided_at"] is not None

    assert sensor_id in await _sensor_ids(service_client, target_project, target_headers)

    # Already attached now.
    resp = await _request(service_client, sensor_id, target_project, target_headers)
    assert resp.status == 409

    # A decided request cannot be decided again.
    resp = await service_client.post(
        f"/api/v1/sensor-project-requests/{item['id']}/reject?project_id={owner_project}",
        headers=owner_headers,
    )
    assert resp.status == 409


@pytest.mark.asyncio
async def test_reject_keeps_sensor_out(service_client):
    owner_project, owner_headers, target_project, target_headers, sensor_id = await _setup(
        service_client
    )
    request_id = (await (await _request(service_client, sensor_id, target_project, target_headers)).json())["id"]

    resp = await service_client.post(
        f"/api/v1/sensor-project-requests/{request_id}/reject?project_id={owner_project}",
        headers=owner_headers,
    )
    assert resp.status == 200
    assert (await resp.json())["status"] == "rejected"
    assert sensor_id not in await _sensor_ids(service_client, target_project, target_headers)

    # A new request can be opened after a rejection.
    resp = await _request(service_client, sensor_id, target_project, target_headers)
    assert resp.status == 202
    assert (await resp.json())["id"] != request_id


@pytest.mark.asyncio
async def test_requester_cannot_approve_own_request(service_client):
    _owner_project, _owner_headers, target_project, target_headers, sensor_id = await _setup(
        service_client
    )
    request_id = (await (await _request(service_client, sensor_id, target_project, target_headers)).json())["id"]

    for action in ("approve", "reject"):
        resp = await service_client.post(
            f"/api/v1/sensor-project-requests/{request_id}/{action}?project_id={target_project}",
            headers=target_headers,
        )
        assert resp.status == 404

    # A third project that does not own the sensor cannot approve either.
    other_project = uuid.uuid4()
    resp = await service_client.post(
        f"/api/v1/sensor-project-requests/{request_id}/approve?project_id={other_project}",
        headers=make_headers(other_project),
    )
    assert resp.status == 404
    assert sensor_id not in await _sensor_ids(service_client, target_project, target_headers)


@pytest.mark.asyncio
async def test_any_owning_project_can_approve(service_client):
    owner_project, owner_headers, second_project, second_headers, sensor_id = await _setup(
        service_client
    )
    first_id = (await (await _request(service_client, sensor_id, second_project, second_headers)).json())["id"]
    resp = await service_client.post(
        f"/api/v1/sensor-project-requests/{first_id}/approve?project_id={owner_project}",
        headers=owner_headers,
    )
    assert resp.status == 200

    # The second project now owns the sensor too and can approve a third one.
    third_project = uuid.uuid4()
    third_headers = make_headers(third_project)
    third_id = (await (await _request(service_client, sensor_id, third_project, third_headers)).json())["id"]
    resp = await service_client.post(
        f"/api/v1/sensor-project-requests/{third_id}/approve?project_id={second_project}",
        headers=second_headers,
    )
    assert resp.status == 200
    assert sensor_id in await _sensor_ids(service_client, third_project, third_headers)


@pytest.mark.asyncio
async def test_cancel_only_from_requesting_project(service_client):
    owner_project, owner_headers, target_project, target_headers, sensor_id = await _setup(
        service_client
    )
    request_id = (await (await _request(service_client, sensor_id, target_project, target_headers)).json())["id"]

    resp = await service_client.post(
        f"/api/v1/sensor-project-requests/{request_id}/cancel?project_id={owner_project}",
        headers=owner_headers,
    )
    assert resp.status == 404

    resp = await service_client.post(
        f"/api/v1/sensor-project-requests/{request_id}/cancel?project_id={target_project}",
        headers=target_headers,
    )
    assert resp.status == 200
    assert (await resp.json())["status"] == "cancelled"

    resp = await service_client.post(
        f"/api/v1/sensor-project-requests/{request_id}/approve?project_id={owner_project}",
        headers=owner_headers,
    )
    assert resp.status == 409


@pytest.mark.asyncio
async def test_permissions(service_client):
    owner_project, _owner_headers, target_project, target_headers, sensor_id = await _setup(
        service_client
    )
    resp = await _request(
        service_client, sensor_id, target_project, make_headers(target_project, role="viewer")
    )
    assert resp.status == 403

    request_id = (await (await _request(service_client, sensor_id, target_project, target_headers)).json())["id"]
    resp = await service_client.post(
        f"/api/v1/sensor-project-requests/{request_id}/approve?project_id={owner_project}",
        headers=make_headers(owner_project, role="viewer"),
    )
    assert resp.status == 403

    resp = await service_client.post(
        f"/api/v1/sensor-project-requests/{request_id}/cancel?project_id={target_project}",
        headers=make_headers(target_project, role="viewer"),
    )
    assert resp.status == 403


@pytest.mark.asyncio
async def test_validation_and_missing(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    resp = await service_client.get(
        f"/api/v1/sensor-project-requests?project_id={project_id}&direction=sideways",
        headers=headers,
    )
    assert resp.status == 400
    resp = await service_client.get(
        f"/api/v1/sensor-project-requests?project_id={project_id}&status=maybe",
        headers=headers,
    )
    assert resp.status == 400
    resp = await service_client.post(
        f"/api/v1/sensor-project-requests/{uuid.uuid4()}/approve?project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 404
    resp = await service_client.post(
        f"/api/v1/sensors/{uuid.uuid4()}/projects", json={}, headers=headers,
    )
    assert resp.status == 400


@pytest.mark.asyncio
async def test_detach_from_other_project_only_by_primary_project(service_client):
    owner_project, owner_headers, target_project, target_headers, sensor_id = await _setup(
        service_client
    )
    request_id = (await (await _request(service_client, sensor_id, target_project, target_headers)).json())["id"]
    await service_client.post(
        f"/api/v1/sensor-project-requests/{request_id}/approve?project_id={owner_project}",
        headers=owner_headers,
    )

    # The attached (non-primary) project cannot detach the sensor from its primary project.
    resp = await service_client.delete(
        f"/api/v1/sensors/{sensor_id}/projects/{owner_project}?project_id={target_project}",
        headers=target_headers,
    )
    assert resp.status == 403

    # A stranger cannot detach it from anywhere.
    stranger = uuid.uuid4()
    resp = await service_client.delete(
        f"/api/v1/sensors/{sensor_id}/projects/{target_project}?project_id={stranger}",
        headers=make_headers(stranger),
    )
    assert resp.status == 404

    # The primary project can revoke the attachment.
    resp = await service_client.delete(
        f"/api/v1/sensors/{sensor_id}/projects/{target_project}?project_id={owner_project}",
        headers=owner_headers,
    )
    assert resp.status == 204
    assert sensor_id not in await _sensor_ids(service_client, target_project, target_headers)
