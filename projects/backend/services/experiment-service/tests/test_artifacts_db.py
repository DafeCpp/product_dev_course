"""Artifacts API against a real database: CRUD, approval, presigned URLs with a fake S3 client."""
from __future__ import annotations

import uuid

import pytest

from experiment_service.api.routes import artifacts as artifacts_routes
from tests.utils import make_headers


class FakeS3:
    def __init__(self, fail: bool = False) -> None:
        self.fail = fail

    async def presign_upload(self, key: str, content_type: str) -> str:
        if self.fail:
            raise RuntimeError("s3 down")
        return f"https://s3.test/upload/{key}?ct={content_type}"

    async def presign_download(self, key: str, filename: str | None = None) -> str:
        if self.fail:
            raise RuntimeError("s3 down")
        return f"https://s3.test/download/{key}?name={filename}"


@pytest.fixture
def project_id() -> uuid.UUID:
    return uuid.uuid4()


@pytest.fixture
def headers(project_id):
    return make_headers(project_id)


@pytest.fixture
async def run_id(service_client, project_id, headers) -> str:
    response = await service_client.post(
        "/api/v1/experiments", json={"project_id": str(project_id), "name": "exp"}, headers=headers
    )
    experiment = await response.json()
    response = await service_client.post(
        f"/api/v1/experiments/{experiment['id']}/runs", json={"name": "run"}, headers=headers
    )
    return (await response.json())["id"]


async def create_artifact(client, run_id, headers, **extra) -> dict:
    body = {"type": "model", "uri": "file:///tmp/a.bin", **extra}
    response = await client.post(f"/api/v1/runs/{run_id}/artifacts", json=body, headers=headers)
    assert response.status == 201, await response.text()
    return await response.json()


async def test_create_and_validate_artifact(service_client, run_id, headers):
    created = await create_artifact(
        service_client, run_id, headers, checksum="abc", size_bytes=10, metadata={"k": "v"}, is_restricted=True
    )
    assert (created["checksum"], created["size_bytes"], created["metadata"]) == ("abc", 10, {"k": "v"})
    assert created["is_restricted"] is True

    for body in ({"uri": "x"}, {"type": "model"}, {"type": 1, "uri": "x"}, {"type": "m", "uri": 2}):
        response = await service_client.post(f"/api/v1/runs/{run_id}/artifacts", json=body, headers=headers)
        assert response.status == 400, body

    response = await service_client.post(
        f"/api/v1/runs/{uuid.uuid4()}/artifacts", json={"type": "model", "uri": "x"}, headers=headers
    )
    assert response.status == 404


async def test_list_artifacts_filters_and_pages(service_client, run_id, headers):
    for index in range(3):
        await create_artifact(service_client, run_id, headers, type="model", uri=f"file:///m{index}")
    await create_artifact(service_client, run_id, headers, type="log", uri="file:///l")
    base = f"/api/v1/runs/{run_id}/artifacts"

    response = await service_client.get(f"{base}?type=model&limit=2", headers=headers)
    body = await response.json()
    assert (len(body["artifacts"]), body["total"]) == (2, 3)

    # An offset past the last row leaves no window total, so a count query runs instead.
    response = await service_client.get(f"{base}?type=model&offset=10", headers=headers)
    body = await response.json()
    assert (body["artifacts"], body["total"]) == ([], 3)

    response = await service_client.get(base, headers=headers)
    assert (await response.json())["total"] == 4


async def test_get_approve_delete_artifact(service_client, run_id, headers):
    created = await create_artifact(service_client, run_id, headers)
    url = f"/api/v1/artifacts/{created['id']}"

    response = await service_client.get(url, headers=headers)
    assert response.status == 200
    response = await service_client.get(f"/api/v1/artifacts/{uuid.uuid4()}", headers=headers)
    assert response.status == 404

    response = await service_client.post(f"{url}/approve", json={"note": "looks good"}, headers=headers)
    assert response.status == 200
    approved = await response.json()
    assert approved["approval_note"] == "looks good" and approved["approved_by"]

    response = await service_client.post(f"{url}/approve", json={}, headers=headers)
    assert response.status == 200
    response = await service_client.post(f"/api/v1/artifacts/{uuid.uuid4()}/approve", json={}, headers=headers)
    assert response.status == 404

    response = await service_client.delete(url, headers=headers)
    assert response.status == 204
    response = await service_client.delete(url, headers=headers)
    assert response.status == 404


async def test_request_upload_url(service_client, run_id, headers, monkeypatch):
    monkeypatch.setattr(artifacts_routes, "get_s3_client", lambda: FakeS3())
    url = f"/api/v1/runs/{run_id}/artifacts/upload-url"

    response = await service_client.post(
        url,
        json={"filename": "w.bin", "type": "model", "content_type": "text/plain", "size_bytes": 5},
        headers=headers,
    )
    assert response.status == 201
    body = await response.json()
    assert body["s3_key"].endswith("/w.bin") and body["upload_url"].startswith("https://s3.test/upload/")

    response = await service_client.get(f"/api/v1/artifacts/{body['artifact_id']}", headers=headers)
    assert (await response.json())["uri"] == f"s3://{body['s3_key']}"

    for bad in ({"type": "model"}, {"filename": "w.bin"}):
        response = await service_client.post(url, json=bad, headers=headers)
        assert response.status == 400, bad

    response = await service_client.post(
        f"/api/v1/runs/{uuid.uuid4()}/artifacts/upload-url",
        json={"filename": "w.bin", "type": "model"},
        headers=headers,
    )
    assert response.status == 404


async def test_upload_url_reports_s3_outage(service_client, run_id, headers, monkeypatch):
    monkeypatch.setattr(artifacts_routes, "get_s3_client", lambda: FakeS3(fail=True))
    response = await service_client.post(
        f"/api/v1/runs/{run_id}/artifacts/upload-url", json={"filename": "w.bin", "type": "model"}, headers=headers
    )
    assert response.status == 503


async def test_download_url_variants(service_client, run_id, headers, monkeypatch):
    legacy = await create_artifact(service_client, run_id, headers, uri="file:///legacy.bin")
    response = await service_client.get(f"/api/v1/artifacts/{legacy['id']}/download-url", headers=headers)
    assert await response.json() == {"download_url": "file:///legacy.bin", "expires_in": None}

    stored = await create_artifact(service_client, run_id, headers, uri="s3://bucket/dir/file.bin")
    flat = await create_artifact(service_client, run_id, headers, uri="s3://flat.bin")
    monkeypatch.setattr(artifacts_routes, "get_s3_client", lambda: FakeS3())
    response = await service_client.get(f"/api/v1/artifacts/{stored['id']}/download-url", headers=headers)
    body = await response.json()
    assert body["download_url"].endswith("name=file.bin") and body["expires_in"]
    response = await service_client.get(f"/api/v1/artifacts/{flat['id']}/download-url", headers=headers)
    assert (await response.json())["download_url"].endswith("name=flat.bin")

    monkeypatch.setattr(artifacts_routes, "get_s3_client", lambda: FakeS3(fail=True))
    response = await service_client.get(f"/api/v1/artifacts/{stored['id']}/download-url", headers=headers)
    assert response.status == 503

    response = await service_client.get(f"/api/v1/artifacts/{uuid.uuid4()}/download-url", headers=headers)
    assert response.status == 404
