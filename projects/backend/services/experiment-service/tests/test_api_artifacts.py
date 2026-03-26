"""Unit tests for Artifact Service API (mock-based, no DB required)."""
from __future__ import annotations

import uuid
from datetime import datetime, timezone
from typing import Any
from unittest.mock import AsyncMock, patch

import pytest
from aiohttp import web
from aiohttp.test_utils import TestClient

from experiment_service.api.routes.artifacts import routes
from experiment_service.core.exceptions import NotFoundError
from experiment_service.domain.models import Artifact
from experiment_service.services.artifacts import ArtifactService


# ---------------------------------------------------------------------------
# Constants / helpers
# ---------------------------------------------------------------------------

PROJECT_ID = uuid.uuid4()
RUN_ID = uuid.uuid4()
ARTIFACT_ID = uuid.uuid4()
USER_ID = uuid.uuid4()

_TS = datetime(2025, 1, 1, 0, 0, 0, tzinfo=timezone.utc)

OWNER_HEADERS = {
    "X-User-Id": str(USER_ID),
    "X-Project-Id": str(PROJECT_ID),
    "X-User-Is-Superadmin": "false",
    "X-User-Permissions": (
        "experiments.view,experiments.create,experiments.update,"
        "runs.create,runs.update,metrics.write,"
        "project.roles.manage,project.members.view"
    ),
}

EDITOR_HEADERS = {
    "X-User-Id": str(uuid.uuid4()),
    "X-Project-Id": str(PROJECT_ID),
    "X-User-Is-Superadmin": "false",
    "X-User-Permissions": (
        "experiments.view,experiments.create,experiments.update,"
        "runs.create,runs.update,metrics.write,project.members.view"
    ),
}

VIEWER_HEADERS = {
    "X-User-Id": str(uuid.uuid4()),
    "X-Project-Id": str(PROJECT_ID),
    "X-User-Is-Superadmin": "false",
    "X-User-Permissions": "experiments.view,project.members.view",
}


def _make_artifact(
    *,
    artifact_id: uuid.UUID | None = None,
    type: str = "model",
    approved_by: uuid.UUID | None = None,
) -> Artifact:
    return Artifact(
        id=artifact_id or ARTIFACT_ID,
        run_id=RUN_ID,
        project_id=PROJECT_ID,
        type=type,
        uri="s3://bucket/key",
        checksum="abc123",
        size_bytes=1024,
        metadata={"framework": "pytorch"},
        created_by=USER_ID,
        approved_by=approved_by,
        approval_note=None,
        is_restricted=False,
        created_at=_TS,
        updated_at=_TS,
    )


def _make_mock_service(
    *,
    create_return: Artifact | None = None,
    get_return: Artifact | None = None,
    list_return: tuple[list[Artifact], int] | None = None,
    delete_raises: Exception | None = None,
    approve_return: Artifact | None = None,
    create_raises: Exception | None = None,
    get_raises: Exception | None = None,
) -> ArtifactService:
    mock_artifact_repo = AsyncMock()
    mock_run_repo = AsyncMock()
    svc = ArtifactService(mock_artifact_repo, mock_run_repo)

    artifact = create_return or _make_artifact()
    if create_raises:
        svc.create_artifact = AsyncMock(side_effect=create_raises)  # type: ignore[method-assign]
    else:
        svc.create_artifact = AsyncMock(return_value=artifact)  # type: ignore[method-assign]

    if get_raises:
        svc.get_artifact = AsyncMock(side_effect=get_raises)  # type: ignore[method-assign]
    else:
        svc.get_artifact = AsyncMock(return_value=get_return or artifact)  # type: ignore[method-assign]

    if list_return is None:
        list_return = ([artifact], 1)
    svc.list_artifacts_by_run = AsyncMock(return_value=list_return)  # type: ignore[method-assign]

    if delete_raises:
        svc.delete_artifact = AsyncMock(side_effect=delete_raises)  # type: ignore[method-assign]
    else:
        svc.delete_artifact = AsyncMock(return_value=None)  # type: ignore[method-assign]

    approved = approve_return or _make_artifact(approved_by=USER_ID)
    svc.approve_artifact = AsyncMock(return_value=approved)  # type: ignore[method-assign]

    return svc


@pytest.fixture
async def client(aiohttp_client: Any) -> TestClient:
    app = web.Application()
    app.add_routes(routes)
    return await aiohttp_client(app)


def _patch_service(mock_svc: ArtifactService):  # type: ignore[type-arg]
    return patch(
        "experiment_service.api.routes.artifacts.get_artifact_service",
        new=AsyncMock(return_value=mock_svc),
    )


# ---------------------------------------------------------------------------
# POST /api/v1/runs/{run_id}/artifacts
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_create_artifact(client: TestClient) -> None:
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.post(
            f"/api/v1/runs/{RUN_ID}/artifacts",
            json={"type": "model", "uri": "s3://bucket/key", "checksum": "abc123", "size_bytes": 1024},
            headers=EDITOR_HEADERS,
        )
    assert resp.status == 201
    body = await resp.json()
    assert body["type"] == "model"
    assert body["uri"] == "s3://bucket/key"
    assert body["run_id"] == str(RUN_ID)


@pytest.mark.asyncio
async def test_create_artifact_missing_type(client: TestClient) -> None:
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.post(
            f"/api/v1/runs/{RUN_ID}/artifacts",
            json={"uri": "s3://bucket/key"},
            headers=EDITOR_HEADERS,
        )
    assert resp.status == 400


@pytest.mark.asyncio
async def test_create_artifact_missing_uri(client: TestClient) -> None:
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.post(
            f"/api/v1/runs/{RUN_ID}/artifacts",
            json={"type": "model"},
            headers=EDITOR_HEADERS,
        )
    assert resp.status == 400


@pytest.mark.asyncio
async def test_create_artifact_run_not_found(client: TestClient) -> None:
    svc = _make_mock_service(create_raises=NotFoundError("Run not found"))
    with _patch_service(svc):
        resp = await client.post(
            f"/api/v1/runs/{RUN_ID}/artifacts",
            json={"type": "model", "uri": "s3://bucket/key"},
            headers=EDITOR_HEADERS,
        )
    assert resp.status == 404


# ---------------------------------------------------------------------------
# GET /api/v1/runs/{run_id}/artifacts
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_list_artifacts_by_run(client: TestClient) -> None:
    a1 = _make_artifact(type="model")
    a2 = _make_artifact(artifact_id=uuid.uuid4(), type="dataset")
    svc = _make_mock_service(list_return=([a1, a2], 2))
    with _patch_service(svc):
        resp = await client.get(
            f"/api/v1/runs/{RUN_ID}/artifacts",
            headers=VIEWER_HEADERS,
        )
    assert resp.status == 200
    body = await resp.json()
    assert body["total"] == 2
    assert len(body["artifacts"]) == 2


@pytest.mark.asyncio
async def test_list_artifacts_by_run_with_type_filter(client: TestClient) -> None:
    a1 = _make_artifact(type="model")
    svc = _make_mock_service(list_return=([a1], 1))
    with _patch_service(svc):
        resp = await client.get(
            f"/api/v1/runs/{RUN_ID}/artifacts?type=model",
            headers=VIEWER_HEADERS,
        )
    assert resp.status == 200
    body = await resp.json()
    assert body["total"] == 1
    svc.list_artifacts_by_run.assert_awaited_once()
    call_kwargs = svc.list_artifacts_by_run.call_args.kwargs
    assert call_kwargs.get("type_filter") == "model"


# ---------------------------------------------------------------------------
# GET /api/v1/artifacts/{artifact_id}
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_get_artifact(client: TestClient) -> None:
    artifact = _make_artifact()
    svc = _make_mock_service(get_return=artifact)
    with _patch_service(svc):
        resp = await client.get(
            f"/api/v1/artifacts/{ARTIFACT_ID}",
            headers=VIEWER_HEADERS,
        )
    assert resp.status == 200
    body = await resp.json()
    assert body["id"] == str(ARTIFACT_ID)
    assert body["type"] == "model"


@pytest.mark.asyncio
async def test_get_artifact_not_found(client: TestClient) -> None:
    svc = _make_mock_service(get_raises=NotFoundError("Artifact not found"))
    with _patch_service(svc):
        resp = await client.get(
            f"/api/v1/artifacts/{uuid.uuid4()}",
            headers=VIEWER_HEADERS,
        )
    assert resp.status == 404


# ---------------------------------------------------------------------------
# DELETE /api/v1/artifacts/{artifact_id}
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_delete_artifact(client: TestClient) -> None:
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.delete(
            f"/api/v1/artifacts/{ARTIFACT_ID}",
            headers=EDITOR_HEADERS,
        )
    assert resp.status == 204
    svc.delete_artifact.assert_awaited_once_with(ARTIFACT_ID)


@pytest.mark.asyncio
async def test_delete_artifact_not_found(client: TestClient) -> None:
    svc = _make_mock_service(delete_raises=NotFoundError("Artifact not found"))
    with _patch_service(svc):
        resp = await client.delete(
            f"/api/v1/artifacts/{uuid.uuid4()}",
            headers=EDITOR_HEADERS,
        )
    assert resp.status == 404


# ---------------------------------------------------------------------------
# POST /api/v1/artifacts/{artifact_id}/approve
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_approve_artifact(client: TestClient) -> None:
    approved = _make_artifact(approved_by=USER_ID)
    svc = _make_mock_service(approve_return=approved)
    with _patch_service(svc):
        resp = await client.post(
            f"/api/v1/artifacts/{ARTIFACT_ID}/approve",
            json={"note": "looks good"},
            headers=OWNER_HEADERS,
        )
    assert resp.status == 200
    body = await resp.json()
    assert body["approved_by"] == str(USER_ID)


# ---------------------------------------------------------------------------
# RBAC checks
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_rbac_viewer_cannot_create(client: TestClient) -> None:
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.post(
            f"/api/v1/runs/{RUN_ID}/artifacts",
            json={"type": "model", "uri": "s3://bucket/key"},
            headers=VIEWER_HEADERS,
        )
    assert resp.status == 403


@pytest.mark.asyncio
async def test_rbac_viewer_can_read(client: TestClient) -> None:
    artifact = _make_artifact()
    svc = _make_mock_service(get_return=artifact)
    with _patch_service(svc):
        resp = await client.get(
            f"/api/v1/artifacts/{ARTIFACT_ID}",
            headers=VIEWER_HEADERS,
        )
    assert resp.status == 200


@pytest.mark.asyncio
async def test_rbac_viewer_cannot_delete(client: TestClient) -> None:
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.delete(
            f"/api/v1/artifacts/{ARTIFACT_ID}",
            headers=VIEWER_HEADERS,
        )
    assert resp.status == 403


@pytest.mark.asyncio
async def test_rbac_editor_cannot_approve(client: TestClient) -> None:
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.post(
            f"/api/v1/artifacts/{ARTIFACT_ID}/approve",
            json={},
            headers=EDITOR_HEADERS,
        )
    assert resp.status == 403


@pytest.mark.asyncio
async def test_rbac_unauthenticated_returns_401(client: TestClient) -> None:
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.get(f"/api/v1/artifacts/{ARTIFACT_ID}")
    assert resp.status == 401
