"""Tests for the audit middleware.

Covers ``middleware/audit.py``: route mapping, header parsing, mutating-only
filter, error-response skip, missing/invalid identity skip, project scope
parsing, and the AuditClient hand-off.
"""
from __future__ import annotations

from uuid import UUID, uuid4

import pytest
from aiohttp import web
from aiohttp.test_utils import TestClient, TestServer

from experiment_service.middleware.audit import (
    _AUDIT_CLIENT_KEY,
    audit_middleware,
)


class _StubAuditClient:
    """Captures every log_action call without doing HTTP."""

    def __init__(self) -> None:
        self.calls: list[dict] = []

    def log_action(self, **kwargs) -> None:  # type: ignore[no-untyped-def]
        self.calls.append(kwargs)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


async def _ok_handler(_request: web.Request) -> web.Response:
    return web.Response(status=200, text="ok")


async def _created_handler(_request: web.Request) -> web.Response:
    return web.Response(status=201, text="created")


async def _server_error_handler(_request: web.Request) -> web.Response:
    return web.Response(status=500, text="boom")


def _build_app(stub: _StubAuditClient | None) -> web.Application:
    app = web.Application(middlewares=[audit_middleware])
    if stub is not None:
        app[_AUDIT_CLIENT_KEY] = stub

    app.router.add_post("/api/v1/experiments", _created_handler)
    app.router.add_patch("/api/v1/experiments/{experiment_id}", _ok_handler)
    app.router.add_delete("/api/v1/experiments/{experiment_id}", _ok_handler)
    app.router.add_post(
        "/api/v1/runs/{run_id}/capture-sessions",
        _created_handler,
    )
    app.router.add_get("/api/v1/experiments", _ok_handler)
    app.router.add_post("/api/v1/unmapped-route", _ok_handler)
    app.router.add_post("/api/v1/experiments/{experiment_id}/explode", _server_error_handler)
    return app


@pytest.fixture
async def stub() -> _StubAuditClient:
    return _StubAuditClient()


@pytest.fixture
async def client(stub: _StubAuditClient) -> TestClient:
    server = TestServer(_build_app(stub))
    test_client = TestClient(server)
    await test_client.start_server()
    try:
        yield test_client
    finally:
        await test_client.close()


# ---------------------------------------------------------------------------
# Filter behavior
# ---------------------------------------------------------------------------


async def test_get_request_is_not_audited(stub, client):
    resp = await client.get(
        "/api/v1/experiments",
        headers={"X-User-Id": str(uuid4())},
    )
    assert resp.status == 200
    assert stub.calls == []


async def test_unmapped_route_is_not_audited(stub, client):
    resp = await client.post(
        "/api/v1/unmapped-route",
        headers={"X-User-Id": str(uuid4())},
    )
    assert resp.status == 200
    assert stub.calls == []


async def test_error_response_is_not_audited(stub, client):
    """A 5xx response must not produce an audit entry."""
    resp = await client.post(
        "/api/v1/experiments/00000000-0000-0000-0000-000000000001/explode",
        headers={"X-User-Id": str(uuid4())},
    )
    assert resp.status == 500
    assert stub.calls == []


async def test_missing_user_id_skips_audit(stub, client):
    resp = await client.post("/api/v1/experiments", json={})
    assert resp.status == 201
    assert stub.calls == []


async def test_invalid_user_id_skips_audit(stub, client):
    resp = await client.post(
        "/api/v1/experiments",
        json={},
        headers={"X-User-Id": "not-a-uuid"},
    )
    assert resp.status == 201
    assert stub.calls == []


async def test_audit_client_missing_skips_audit():
    """When no AuditClient is registered the middleware is a passthrough."""
    server = TestServer(_build_app(stub=None))
    test_client = TestClient(server)
    await test_client.start_server()
    try:
        resp = await test_client.post(
            "/api/v1/experiments",
            json={},
            headers={"X-User-Id": str(uuid4())},
        )
        assert resp.status == 201
    finally:
        await test_client.close()


# ---------------------------------------------------------------------------
# Successful audit cases
# ---------------------------------------------------------------------------


async def test_post_experiment_logs_create(stub, client):
    actor = uuid4()
    project = uuid4()
    resp = await client.post(
        "/api/v1/experiments",
        json={},
        headers={
            "X-User-Id": str(actor),
            "X-Project-Id": str(project),
            "X-Forwarded-For": "10.0.0.1, 192.168.1.1",
            "User-Agent": "pytest/1.0",
        },
    )
    assert resp.status == 201
    assert len(stub.calls) == 1
    call = stub.calls[0]
    assert call["actor_id"] == actor
    assert call["action"] == "experiment.create"
    assert call["target_type"] == "experiment"
    assert call["target_id"] is None  # POST /api/v1/experiments has no URL id
    assert call["scope_type"] == "project"
    assert call["scope_id"] == project
    assert call["ip_address"] == "10.0.0.1"  # first hop from X-Forwarded-For
    assert call["user_agent"] == "pytest/1.0"


async def test_patch_experiment_extracts_target_id(stub, client):
    actor = uuid4()
    experiment_id = uuid4()
    resp = await client.patch(
        f"/api/v1/experiments/{experiment_id}",
        json={},
        headers={"X-User-Id": str(actor)},
    )
    assert resp.status == 200
    assert len(stub.calls) == 1
    call = stub.calls[0]
    assert call["action"] == "experiment.update"
    assert call["target_id"] == str(experiment_id)
    # No project header → system scope
    assert call["scope_type"] == "system"
    assert call["scope_id"] is None


async def test_capture_session_target_id_uses_session_param(stub, client):
    """target_id is taken from the session_id URL param, not run_id."""
    actor = uuid4()
    run_id = uuid4()
    resp = await client.post(
        f"/api/v1/runs/{run_id}/capture-sessions",
        json={},
        headers={"X-User-Id": str(actor)},
    )
    assert resp.status == 201
    call = stub.calls[0]
    assert call["action"] == "capture_session.create"
    # POST has no session_id URL param, so target_id is None
    assert call["target_id"] is None


async def test_invalid_project_id_falls_back_to_system_scope(stub, client):
    actor = uuid4()
    resp = await client.post(
        "/api/v1/experiments",
        json={},
        headers={
            "X-User-Id": str(actor),
            "X-Project-Id": "not-a-uuid",
        },
    )
    assert resp.status == 201
    call = stub.calls[0]
    assert call["scope_type"] == "system"
    assert call["scope_id"] is None


async def test_uses_remote_when_no_xforwarded(stub, client):
    actor = uuid4()
    resp = await client.post(
        "/api/v1/experiments",
        json={},
        headers={"X-User-Id": str(actor)},
    )
    assert resp.status == 201
    call = stub.calls[0]
    # request.remote on TestClient is typically '127.0.0.1'
    assert call["ip_address"] is not None


async def test_xforwarded_first_value_is_extracted(stub, client):
    actor = uuid4()
    resp = await client.post(
        "/api/v1/experiments",
        json={},
        headers={
            "X-User-Id": str(actor),
            "X-Forwarded-For": "  203.0.113.7  , 10.0.0.1",
        },
    )
    assert resp.status == 201
    call = stub.calls[0]
    assert call["ip_address"] == "203.0.113.7"


async def test_delete_experiment_action_is_mapped(stub, client):
    actor = uuid4()
    experiment_id = uuid4()
    resp = await client.delete(
        f"/api/v1/experiments/{experiment_id}",
        headers={"X-User-Id": str(actor)},
    )
    assert resp.status == 200
    call = stub.calls[0]
    assert call["action"] == "experiment.delete"
    assert call["target_id"] == str(experiment_id)
