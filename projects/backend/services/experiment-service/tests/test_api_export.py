"""Integration tests for the data export endpoints."""
from __future__ import annotations

import csv
import io
import json
import uuid

import pytest

from tests.utils import make_headers


async def _create_experiment(service_client, project_id, headers, name="Export Test"):
    resp = await service_client.post(
        "/api/v1/experiments",
        json={
            "project_id": str(project_id),
            "name": name,
            "tags": ["test"],
            "metadata": {"key": "value"},
        },
        headers=headers,
    )
    assert resp.status == 201
    return await resp.json()


async def _create_run(service_client, experiment_id, headers, name="Run 1"):
    resp = await service_client.post(
        f"/api/v1/experiments/{experiment_id}/runs",
        json={"name": name, "params": {"lr": 0.01}},
        headers=headers,
    )
    assert resp.status == 201
    return await resp.json()


@pytest.mark.asyncio
async def test_export_experiments_csv(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    exp = await _create_experiment(service_client, project_id, headers)

    resp = await service_client.get(
        f"/api/v1/experiments/export?project_id={project_id}&format=csv",
        headers=headers,
    )
    assert resp.status == 200
    assert "text/csv" in resp.headers.get("Content-Type", "")
    assert "attachment" in resp.headers.get("Content-Disposition", "")

    body = await resp.text()
    reader = csv.reader(io.StringIO(body))
    rows = list(reader)

    # Header row + at least 1 data row
    assert len(rows) >= 2
    header = rows[0]
    assert "id" in header
    assert "name" in header
    assert "status" in header

    # Find the experiment row
    id_idx = header.index("id")
    data_ids = [row[id_idx] for row in rows[1:]]
    assert exp["id"] in data_ids


@pytest.mark.asyncio
async def test_export_experiments_json(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    exp = await _create_experiment(service_client, project_id, headers)

    resp = await service_client.get(
        f"/api/v1/experiments/export?project_id={project_id}&format=json",
        headers=headers,
    )
    assert resp.status == 200
    assert "application/json" in resp.headers.get("Content-Type", "")

    body = await resp.text()
    data = json.loads(body)
    assert isinstance(data, list)
    assert len(data) >= 1

    ids = [e["id"] for e in data]
    assert exp["id"] in ids


@pytest.mark.asyncio
async def test_export_experiments_default_format_is_csv(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    await _create_experiment(service_client, project_id, headers)

    resp = await service_client.get(
        f"/api/v1/experiments/export?project_id={project_id}",
        headers=headers,
    )
    assert resp.status == 200
    assert "text/csv" in resp.headers.get("Content-Type", "")


@pytest.mark.asyncio
async def test_export_experiments_invalid_format(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    resp = await service_client.get(
        f"/api/v1/experiments/export?project_id={project_id}&format=xml",
        headers=headers,
    )
    assert resp.status == 400


@pytest.mark.asyncio
async def test_export_experiments_with_status_filter(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    await _create_experiment(service_client, project_id, headers, name="Draft Exp")

    resp = await service_client.get(
        f"/api/v1/experiments/export?project_id={project_id}&format=json&status=archived",
        headers=headers,
    )
    assert resp.status == 200
    data = json.loads(await resp.text())
    # Should be empty since our experiment is in draft status, not archived
    assert len(data) == 0


@pytest.mark.asyncio
async def test_export_runs_csv(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    exp = await _create_experiment(service_client, project_id, headers)
    run = await _create_run(service_client, exp["id"], headers)

    resp = await service_client.get(
        f"/api/v1/experiments/{exp['id']}/runs/export?format=csv",
        headers=headers,
    )
    assert resp.status == 200
    assert "text/csv" in resp.headers.get("Content-Type", "")

    body = await resp.text()
    reader = csv.reader(io.StringIO(body))
    rows = list(reader)
    assert len(rows) >= 2

    header = rows[0]
    assert "id" in header
    assert "experiment_id" in header

    id_idx = header.index("id")
    data_ids = [row[id_idx] for row in rows[1:]]
    assert run["id"] in data_ids


@pytest.mark.asyncio
async def test_export_runs_json(service_client):
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    exp = await _create_experiment(service_client, project_id, headers)
    run = await _create_run(service_client, exp["id"], headers)

    resp = await service_client.get(
        f"/api/v1/experiments/{exp['id']}/runs/export?format=json",
        headers=headers,
    )
    assert resp.status == 200
    assert "application/json" in resp.headers.get("Content-Type", "")

    data = json.loads(await resp.text())
    assert isinstance(data, list)
    assert len(data) >= 1
    ids = [r["id"] for r in data]
    assert run["id"] in ids


@pytest.mark.asyncio
async def test_export_experiments_empty_project(service_client):
    """Exporting from a project with no experiments should return empty CSV/JSON."""
    project_id = uuid.uuid4()
    headers = make_headers(project_id)

    # CSV
    resp = await service_client.get(
        f"/api/v1/experiments/export?project_id={project_id}&format=csv",
        headers=headers,
    )
    assert resp.status == 200
    body = await resp.text()
    rows = list(csv.reader(io.StringIO(body)))
    assert len(rows) == 1  # Header only

    # JSON
    resp = await service_client.get(
        f"/api/v1/experiments/export?project_id={project_id}&format=json",
        headers=headers,
    )
    assert resp.status == 200
    data = json.loads(await resp.text())
    assert data == []
