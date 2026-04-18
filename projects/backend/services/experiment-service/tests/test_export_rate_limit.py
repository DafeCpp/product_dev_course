"""Tests for the telemetry-export rate limiter.

Unit tests (no DB, no HTTP):
  - requests within the window are accepted
  - the (limit+1)-th request raises HTTP 429
  - the window resets after expiry
  - counters are per-user (different users don't affect each other)

Integration tests (with DB):
  - export endpoints return 429 when the limiter is exhausted
  - the 429 response carries the expected headers
"""
from __future__ import annotations

import time
import uuid

import pytest
from aiohttp import web

from experiment_service.middleware.export_rate_limit import ExportRateLimiter
from tests.utils import make_headers


# ---------------------------------------------------------------------------
# Unit tests — no DB, no aiohttp app
# ---------------------------------------------------------------------------


def test_limiter_allows_requests_within_window():
    limiter = ExportRateLimiter(max_requests=3, window_seconds=60.0)
    uid = uuid.uuid4()
    for _ in range(3):
        limiter.check(uid)  # must not raise


def test_limiter_raises_on_exceeded():
    limiter = ExportRateLimiter(max_requests=2, window_seconds=60.0)
    uid = uuid.uuid4()
    limiter.check(uid)
    limiter.check(uid)
    with pytest.raises(web.HTTPTooManyRequests):
        limiter.check(uid)


def test_limiter_resets_after_window(monkeypatch):
    limiter = ExportRateLimiter(max_requests=1, window_seconds=1.0)
    uid = uuid.uuid4()
    limiter.check(uid)

    # Advance time past the window by monkeypatching time.monotonic
    original = time.monotonic
    monkeypatch.setattr(time, "monotonic", lambda: original() + 2.0)

    limiter.check(uid)  # must not raise — new window


def test_limiter_per_user_isolation():
    limiter = ExportRateLimiter(max_requests=1, window_seconds=60.0)
    user_a = uuid.uuid4()
    user_b = uuid.uuid4()
    limiter.check(user_a)  # consumes user_a's quota
    limiter.check(user_b)  # user_b still has quota — must not raise


def test_limiter_429_has_rate_limit_headers():
    limiter = ExportRateLimiter(max_requests=1, window_seconds=60.0)
    uid = uuid.uuid4()
    limiter.check(uid)
    try:
        limiter.check(uid)
        pytest.fail("Expected HTTPTooManyRequests")
    except web.HTTPTooManyRequests as exc:
        assert "X-RateLimit-Limit" in exc.headers
        assert exc.headers["X-RateLimit-Limit"] == "1"
        assert "X-RateLimit-Remaining" in exc.headers
        assert exc.headers["X-RateLimit-Remaining"] == "0"
        assert "Retry-After" in exc.headers


# ---------------------------------------------------------------------------
# Integration tests — DB required
# ---------------------------------------------------------------------------


async def test_export_session_returns_429_when_limit_exceeded(
    service_client,
    monkeypatch,
):
    """Exhaust the limiter so the next request gets 429."""
    from experiment_service.api.routes.telemetry_export import _export_limiter
    from experiment_service.middleware.export_rate_limit import ExportRateLimiter

    # Replace the module-level limiter with a limit-1 instance so we don't
    # need to make 10 real HTTP requests.
    tight = ExportRateLimiter(max_requests=1, window_seconds=60.0)
    monkeypatch.setattr(
        "experiment_service.api.routes.telemetry_export._export_limiter",
        tight,
    )

    project_id = uuid.uuid4()
    user_id = uuid.uuid4()
    headers = make_headers(project_id, user_id=user_id)

    # First request: consume the quota (run/session will not exist → 404)
    run_id = uuid.uuid4()
    session_id = uuid.uuid4()
    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export",
        params={"project_id": str(project_id)},
        headers=headers,
    )
    # Could be 404 (not found) — that's fine, quota was consumed
    assert resp.status in (200, 404)

    # Second request: must be 429
    resp2 = await service_client.get(
        f"/api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export",
        params={"project_id": str(project_id)},
        headers=headers,
    )
    assert resp2.status == 429
    assert "X-RateLimit-Limit" in resp2.headers
    assert "Retry-After" in resp2.headers


async def test_export_run_returns_429_when_limit_exceeded(
    service_client,
    monkeypatch,
):
    from experiment_service.middleware.export_rate_limit import ExportRateLimiter

    tight = ExportRateLimiter(max_requests=1, window_seconds=60.0)
    monkeypatch.setattr(
        "experiment_service.api.routes.telemetry_export._export_limiter",
        tight,
    )

    project_id = uuid.uuid4()
    user_id = uuid.uuid4()
    headers = make_headers(project_id, user_id=user_id)
    run_id = uuid.uuid4()

    resp = await service_client.get(
        f"/api/v1/runs/{run_id}/telemetry/export",
        params={"project_id": str(project_id)},
        headers=headers,
    )
    assert resp.status in (200, 404)

    resp2 = await service_client.get(
        f"/api/v1/runs/{run_id}/telemetry/export",
        params={"project_id": str(project_id)},
        headers=headers,
    )
    assert resp2.status == 429


async def test_export_different_users_have_separate_quotas(
    service_client,
    monkeypatch,
):
    from experiment_service.middleware.export_rate_limit import ExportRateLimiter

    tight = ExportRateLimiter(max_requests=1, window_seconds=60.0)
    monkeypatch.setattr(
        "experiment_service.api.routes.telemetry_export._export_limiter",
        tight,
    )

    project_id = uuid.uuid4()
    run_id = uuid.uuid4()
    user_a = uuid.uuid4()
    user_b = uuid.uuid4()

    headers_a = make_headers(project_id, user_id=user_a)
    headers_b = make_headers(project_id, user_id=user_b)

    # User A consumes their quota
    resp_a = await service_client.get(
        f"/api/v1/runs/{run_id}/telemetry/export",
        params={"project_id": str(project_id)},
        headers=headers_a,
    )
    assert resp_a.status in (200, 404)

    # User B still has quota — must NOT be 429
    resp_b = await service_client.get(
        f"/api/v1/runs/{run_id}/telemetry/export",
        params={"project_id": str(project_id)},
        headers=headers_b,
    )
    assert resp_b.status != 429
