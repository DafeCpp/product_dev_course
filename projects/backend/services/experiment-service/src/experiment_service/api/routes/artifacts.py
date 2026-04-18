"""Artifact endpoints."""
from __future__ import annotations

import uuid

from aiohttp import web
from pydantic import ValidationError

from experiment_service.api.utils import (
    paginated_response,
    pagination_params,
    parse_uuid,
    read_json,
)
from experiment_service.core.exceptions import NotFoundError
from experiment_service.core.s3_client import get_s3_client
from experiment_service.services.dependencies import (
    ensure_permission,
    get_artifact_service,
    require_current_user,
    resolve_project_id,
)

routes = web.RouteTableDef()


def _artifact_response(artifact: object) -> dict:  # type: ignore[type-arg]
    from experiment_service.domain.models import Artifact
    assert isinstance(artifact, Artifact)
    return artifact.model_dump(mode="json")


@routes.post("/api/v1/runs/{run_id}/artifacts")
async def create_artifact(request: web.Request) -> web.Response:
    """Create artifact record (editor+)."""
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_permission(user, "runs.update")
    run_id = parse_uuid(request.match_info["run_id"], "run_id")
    body = await read_json(request)

    type_ = body.get("type")
    uri = body.get("uri")
    if not type_ or not isinstance(type_, str):
        raise web.HTTPBadRequest(text="type is required")
    if not uri or not isinstance(uri, str):
        raise web.HTTPBadRequest(text="uri is required")

    checksum: str | None = body.get("checksum")
    size_bytes: int | None = body.get("size_bytes")
    metadata: dict = body.get("metadata") or {}
    is_restricted: bool = bool(body.get("is_restricted", False))

    service = await get_artifact_service(request)
    try:
        artifact = await service.create_artifact(
            project_id=project_id,
            run_id=run_id,
            type=type_,
            uri=uri,
            created_by=user.user_id,
            checksum=checksum,
            size_bytes=size_bytes,
            metadata=metadata,
            is_restricted=is_restricted,
        )
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.json_response(_artifact_response(artifact), status=201)


@routes.get("/api/v1/runs/{run_id}/artifacts")
async def list_artifacts(request: web.Request) -> web.Response:
    """List artifacts for a run (viewer+)."""
    user = await require_current_user(request)
    resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_permission(user, "experiments.view")
    run_id = parse_uuid(request.match_info["run_id"], "run_id")
    limit, offset = pagination_params(request)
    type_filter: str | None = request.rel_url.query.get("type") or None

    service = await get_artifact_service(request)
    artifacts, total = await service.list_artifacts_by_run(
        run_id,
        type_filter=type_filter,
        limit=limit,
        offset=offset,
    )
    payload = paginated_response(
        [_artifact_response(a) for a in artifacts],
        limit=limit,
        offset=offset,
        key="artifacts",
        total=total,
    )
    return web.json_response(payload)


@routes.get("/api/v1/artifacts/{artifact_id}")
async def get_artifact(request: web.Request) -> web.Response:
    """Get artifact details (viewer+)."""
    user = await require_current_user(request)
    resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_permission(user, "experiments.view")
    artifact_id = parse_uuid(request.match_info["artifact_id"], "artifact_id")

    service = await get_artifact_service(request)
    try:
        artifact = await service.get_artifact(artifact_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.json_response(_artifact_response(artifact))


@routes.delete("/api/v1/artifacts/{artifact_id}")
async def delete_artifact(request: web.Request) -> web.Response:
    """Delete artifact (editor+)."""
    user = await require_current_user(request)
    resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_permission(user, "runs.update")
    artifact_id = parse_uuid(request.match_info["artifact_id"], "artifact_id")

    service = await get_artifact_service(request)
    try:
        await service.delete_artifact(artifact_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.Response(status=204)


@routes.post("/api/v1/artifacts/{artifact_id}/approve")
async def approve_artifact(request: web.Request) -> web.Response:
    """Approve artifact (owner)."""
    user = await require_current_user(request)
    resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_permission(user, "project.roles.manage")
    artifact_id = parse_uuid(request.match_info["artifact_id"], "artifact_id")
    body = await read_json(request)
    note: str | None = body.get("note") if body else None

    service = await get_artifact_service(request)
    try:
        artifact = await service.approve_artifact(artifact_id, user.user_id, note)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    return web.json_response(_artifact_response(artifact))


@routes.post("/api/v1/runs/{run_id}/artifacts/upload-url")
async def request_upload_url(request: web.Request) -> web.Response:
    """Generate presigned upload URL and create pending artifact record (editor+).

    Body:
        filename: str — original filename (used as S3 key suffix)
        content_type: str — MIME type, e.g. "application/octet-stream"
        type: str — artifact type (model, dataset, log, etc.)
        size_bytes: int | None
        metadata: dict | None
        is_restricted: bool (default false)

    Returns:
        upload_url: presigned PUT URL
        artifact_id: created artifact UUID
        s3_key: the object key in S3
    """
    user = await require_current_user(request)
    project_id = resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_permission(user, "runs.update")
    run_id = parse_uuid(request.match_info["run_id"], "run_id")
    body = await read_json(request)

    filename: str = body.get("filename") or ""
    content_type: str = body.get("content_type") or "application/octet-stream"
    artifact_type: str = body.get("type") or ""
    if not filename or not isinstance(filename, str):
        raise web.HTTPBadRequest(text="filename is required")
    if not artifact_type or not isinstance(artifact_type, str):
        raise web.HTTPBadRequest(text="type is required")

    size_bytes: int | None = body.get("size_bytes")
    metadata: dict = body.get("metadata") or {}
    is_restricted: bool = bool(body.get("is_restricted", False))

    # Generate S3 key: artifacts/{project_id}/{run_id}/{uuid}/{filename}
    object_key = f"artifacts/{project_id}/{run_id}/{uuid.uuid4()}/{filename}"
    s3_uri = f"s3://{object_key}"

    service = await get_artifact_service(request)
    try:
        artifact = await service.create_artifact(
            project_id=project_id,
            run_id=run_id,
            type=artifact_type,
            uri=s3_uri,
            created_by=user.user_id,
            size_bytes=size_bytes,
            metadata=metadata,
            is_restricted=is_restricted,
        )
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc

    s3 = get_s3_client()
    try:
        upload_url = await s3.presign_upload(object_key, content_type)
    except Exception as exc:
        raise web.HTTPServiceUnavailable(text=f"S3 unavailable: {exc}") from exc

    return web.json_response({
        "upload_url": upload_url,
        "artifact_id": str(artifact.id),
        "s3_key": object_key,
    }, status=201)


@routes.get("/api/v1/artifacts/{artifact_id}/download-url")
async def get_download_url(request: web.Request) -> web.Response:
    """Generate presigned download URL for an artifact (viewer+).

    Returns:
        download_url: presigned GET URL
        expires_in: seconds until URL expires
    """
    user = await require_current_user(request)
    resolve_project_id(user, request.rel_url.query.get("project_id"))
    ensure_permission(user, "experiments.view")
    artifact_id = parse_uuid(request.match_info["artifact_id"], "artifact_id")

    service = await get_artifact_service(request)
    try:
        artifact = await service.get_artifact(artifact_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc

    # URI format: "s3://<key>" or legacy plain string
    uri = artifact.uri
    if not uri.startswith("s3://"):
        # Legacy artifact — return URI directly (not presigned)
        return web.json_response({"download_url": uri, "expires_in": None})

    object_key = uri[len("s3://"):]
    # Extract filename from key (last path component)
    filename = object_key.split("/")[-1] if "/" in object_key else object_key

    from experiment_service.settings import settings as _settings
    s3 = get_s3_client()
    try:
        download_url = await s3.presign_download(object_key, filename=filename)
    except Exception as exc:
        raise web.HTTPServiceUnavailable(text=f"S3 unavailable: {exc}") from exc

    return web.json_response({
        "download_url": download_url,
        "expires_in": _settings.s3_presign_expire_seconds,
    })
