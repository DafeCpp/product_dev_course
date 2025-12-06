from __future__ import annotations

# pyright: reportMissingImports=false

import uuid

import pytest

from tests.utils import make_headers


async def _bootstrap_run(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)
    resp = await service_client.post(
        "/api/v1/experiments",
        json={"project_id": str(project_id), "name": "metrics-exp"},
        headers=headers,
    )
    assert resp.status == 201
    experiment_id = (await resp.json())["id"]

    resp_run = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/runs",
        json={"name": "metrics-run"},
        headers=headers,
    )
    assert resp_run.status == 201
    run_id = (await resp_run.json())["id"]
    return {"project_id": project_id, "headers": headers, "run_id": run_id}


@pytest.mark.asyncio
async def test_metrics_ingest_and_query(service_client):
    ctx = await _bootstrap_run(service_client)
    run_id = ctx["run_id"]
    headers = ctx["headers"]
    payload = {
        "metrics": [
            {"name": "loss", "step": 1, "value": 0.5, "timestamp": "2025-01-01T00:00:00Z"},
            {"name": "loss", "step": 2, "value": 0.45, "timestamp": "2025-01-01T00:01:00Z"},
            {"name": "accuracy", "step": 1, "value": 0.9, "timestamp": "2025-01-01T00:00:00Z"},
        ]
    }
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/metrics",
        json=payload,
        headers=headers,
    )
    assert resp.status == 202
    body = await resp.json()
    assert body["accepted"] == 3

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/metrics",
        headers=headers,
    )
    assert resp.status == 200
    series = (await resp.json())["series"]
    assert any(item["name"] == "loss" for item in series)
    loss_series = next(item for item in series if item["name"] == "loss")
    assert len(loss_series["points"]) == 2


@pytest.mark.asyncio
async def test_metrics_ingest_requires_points(service_client):
    ctx = await _bootstrap_run(service_client)
    run_id = ctx["run_id"]
    headers = ctx["headers"]
    resp = await service_client.post(
        f"/api/v1/runs/{run_id}/metrics",
        json={"metrics": []},
        headers=headers,
    )
    assert resp.status == 400

