"""Unit tests for Metrics Service API (mock-based, no DB required)."""
from __future__ import annotations

# pyright: reportMissingImports=false

import uuid
from datetime import datetime, timezone
from typing import Any
from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from aiohttp import web
from aiohttp.test_utils import TestClient, TestServer

from experiment_service.api.routes.metrics import routes
from experiment_service.core.exceptions import NotFoundError
from experiment_service.domain.models import RunMetric
from experiment_service.services.metrics import MetricsService


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

PROJECT_ID = uuid.uuid4()
RUN_ID = uuid.uuid4()

OWNER_HEADERS = {
    "X-User-Id": str(uuid.uuid4()),
    "X-Project-Id": str(PROJECT_ID),
    "X-User-Is-Superadmin": "false",
    "X-User-Permissions": (
        "experiments.view,experiments.create,experiments.update,"
        "runs.create,runs.update,metrics.write"
    ),
}

VIEWER_HEADERS = {
    "X-User-Id": str(uuid.uuid4()),
    "X-Project-Id": str(PROJECT_ID),
    "X-User-Is-Superadmin": "false",
    "X-User-Permissions": "experiments.view,project.members.view",
}

_TS = datetime(2025, 1, 1, 0, 0, 0, tzinfo=timezone.utc)


def _make_run_metric(name: str = "loss", step: int = 1, value: float = 0.5) -> RunMetric:
    return RunMetric(
        id=1,
        project_id=PROJECT_ID,
        run_id=RUN_ID,
        name=name,
        step=step,
        value=value,
        timestamp=_TS,
        created_at=_TS,
    )


def _make_mock_service(
    *,
    ingest_return: int = 3,
    query_return: dict | None = None,
    summary_return: dict | None = None,
    aggregations_return: dict | None = None,
    raise_not_found: bool = False,
) -> MetricsService:
    """Build a MetricsService with mocked repositories."""
    mock_run_repo = MagicMock()
    mock_metrics_repo = MagicMock()

    if raise_not_found:
        mock_run_repo.get = AsyncMock(side_effect=NotFoundError("run not found"))
    else:
        mock_run_repo.get = AsyncMock(return_value=MagicMock())

    mock_metrics_repo.bulk_insert = AsyncMock(return_value=None)
    mock_metrics_repo.count_series = AsyncMock(return_value=3)
    mock_metrics_repo.fetch_series = AsyncMock(
        return_value=[
            _make_run_metric("loss", 1, 0.9),
            _make_run_metric("loss", 2, 0.8),
            _make_run_metric("accuracy", 1, 0.7),
        ]
    )

    if query_return is None:
        query_return = {
            "run_id": str(RUN_ID),
            "series": [
                {"name": "loss", "points": [{"step": 1, "value": 0.9, "timestamp": _TS.isoformat()}]},
            ],
            "total": 3,
            "limit": 1000,
            "offset": 0,
        }

    if summary_return is None:
        summary_return = {
            "run_id": str(RUN_ID),
            "metrics": [
                {
                    "name": "loss",
                    "last_step": 3,
                    "last_value": 0.1,
                    "last_timestamp": _TS.isoformat(),
                    "total_steps": 3,
                    "min_value": 0.1,
                    "max_value": 0.9,
                    "avg_value": 0.5,
                }
            ],
        }

    if aggregations_return is None:
        aggregations_return = {
            "run_id": str(RUN_ID),
            "bucket_size": 10,
            "series": [
                {
                    "name": "loss",
                    "buckets": [
                        {"step_from": 0, "step_to": 9, "min": 0.5, "avg": 0.7, "max": 0.9, "count": 10}
                    ],
                }
            ],
        }

    svc = MetricsService(mock_run_repo, mock_metrics_repo)
    # Patch service methods directly for cleaner tests
    svc.ingest_metrics = AsyncMock(return_value=ingest_return)  # type: ignore[method-assign]
    svc.query_metrics = AsyncMock(return_value=query_return)  # type: ignore[method-assign]
    svc.get_summary = AsyncMock(return_value=summary_return)  # type: ignore[method-assign]
    svc.get_aggregations = AsyncMock(return_value=aggregations_return)  # type: ignore[method-assign]
    return svc


@pytest.fixture
async def client(aiohttp_client: Any) -> TestClient:
    """Minimal aiohttp app with metrics routes only."""
    app = web.Application()
    app.add_routes(routes)
    return await aiohttp_client(app)


# ---------------------------------------------------------------------------
# Helper
# ---------------------------------------------------------------------------

def _patch_service(mock_svc: MetricsService):
    """Context manager: replace get_metrics_service dependency."""
    return patch(
        "experiment_service.api.routes.metrics.get_metrics_service",
        new=AsyncMock(return_value=mock_svc),
    )


# ---------------------------------------------------------------------------
# POST /api/v1/runs/{run_id}/metrics
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_record_metrics(client: TestClient) -> None:
    """POST batch of metrics returns 202 with accepted count."""
    svc = _make_mock_service(ingest_return=3)
    with _patch_service(svc):
        resp = await client.post(
            f"/api/v1/runs/{RUN_ID}/metrics",
            json={
                "metrics": [
                    {"name": "loss", "step": 1, "value": 0.9, "timestamp": "2025-01-01T00:00:00Z"},
                    {"name": "loss", "step": 2, "value": 0.8, "timestamp": "2025-01-01T00:01:00Z"},
                    {"name": "accuracy", "step": 1, "value": 0.7, "timestamp": "2025-01-01T00:00:00Z"},
                ]
            },
            headers=OWNER_HEADERS,
        )
    assert resp.status == 202
    body = await resp.json()
    assert body["status"] == "accepted"
    assert body["accepted"] == 3


@pytest.mark.asyncio
async def test_record_metrics_empty_batch(client: TestClient) -> None:
    """POST with empty metrics array returns 400."""
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.post(
            f"/api/v1/runs/{RUN_ID}/metrics",
            json={"metrics": []},
            headers=OWNER_HEADERS,
        )
    assert resp.status == 400


@pytest.mark.asyncio
async def test_record_metrics_missing_array(client: TestClient) -> None:
    """POST without metrics key returns 400."""
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.post(
            f"/api/v1/runs/{RUN_ID}/metrics",
            json={},
            headers=OWNER_HEADERS,
        )
    assert resp.status == 400


@pytest.mark.asyncio
async def test_record_metrics_run_not_found(client: TestClient) -> None:
    """POST when run doesn't exist returns 404."""
    svc = _make_mock_service()
    svc.ingest_metrics = AsyncMock(side_effect=NotFoundError("run not found"))  # type: ignore[method-assign]
    with _patch_service(svc):
        resp = await client.post(
            f"/api/v1/runs/{RUN_ID}/metrics",
            json={
                "metrics": [
                    {"name": "loss", "step": 1, "value": 0.9, "timestamp": "2025-01-01T00:00:00Z"},
                ]
            },
            headers=OWNER_HEADERS,
        )
    assert resp.status == 404


# ---------------------------------------------------------------------------
# GET /api/v1/runs/{run_id}/metrics
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_list_metrics(client: TestClient) -> None:
    """GET metrics returns series with pagination metadata."""
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.get(
            f"/api/v1/runs/{RUN_ID}/metrics?names=loss,accuracy&from_step=0&to_step=100&order=asc",
            headers=OWNER_HEADERS,
        )
    assert resp.status == 200
    body = await resp.json()
    assert "series" in body
    assert "total" in body
    assert "limit" in body
    assert "offset" in body
    svc.query_metrics.assert_awaited_once()
    call_kwargs = svc.query_metrics.call_args.kwargs
    assert call_kwargs["names"] == ["loss", "accuracy"]
    assert call_kwargs["from_step"] == 0
    assert call_kwargs["to_step"] == 100
    assert call_kwargs["order"] == "asc"


@pytest.mark.asyncio
async def test_list_metrics_pagination(client: TestClient) -> None:
    """GET metrics passes limit and offset to service."""
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.get(
            f"/api/v1/runs/{RUN_ID}/metrics?limit=5&offset=10",
            headers=OWNER_HEADERS,
        )
    assert resp.status == 200
    call_kwargs = svc.query_metrics.call_args.kwargs
    assert call_kwargs["limit"] == 5
    assert call_kwargs["offset"] == 10


@pytest.mark.asyncio
async def test_list_metrics_invalid_order(client: TestClient) -> None:
    """GET metrics with invalid order returns 400."""
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.get(
            f"/api/v1/runs/{RUN_ID}/metrics?order=random",
            headers=OWNER_HEADERS,
        )
    assert resp.status == 400


# ---------------------------------------------------------------------------
# GET /api/v1/runs/{run_id}/metrics/summary
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_metrics_summary(client: TestClient) -> None:
    """GET summary returns per-metric stats."""
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.get(
            f"/api/v1/runs/{RUN_ID}/metrics/summary?names=loss",
            headers=OWNER_HEADERS,
        )
    assert resp.status == 200
    body = await resp.json()
    assert body["run_id"] == str(RUN_ID)
    assert "metrics" in body
    assert len(body["metrics"]) == 1
    m = body["metrics"][0]
    assert m["name"] == "loss"
    assert m["last_step"] == 3
    assert m["total_steps"] == 3
    svc.get_summary.assert_awaited_once()
    call_kwargs = svc.get_summary.call_args.kwargs
    assert call_kwargs["names"] == ["loss"]


@pytest.mark.asyncio
async def test_metrics_summary_no_filter(client: TestClient) -> None:
    """GET summary without names returns all metrics."""
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.get(
            f"/api/v1/runs/{RUN_ID}/metrics/summary",
            headers=OWNER_HEADERS,
        )
    assert resp.status == 200
    call_kwargs = svc.get_summary.call_args.kwargs
    assert call_kwargs["names"] is None


@pytest.mark.asyncio
async def test_metrics_summary_not_found(client: TestClient) -> None:
    """GET summary for missing run returns 404."""
    svc = _make_mock_service()
    svc.get_summary = AsyncMock(side_effect=NotFoundError("run not found"))  # type: ignore[method-assign]
    with _patch_service(svc):
        resp = await client.get(
            f"/api/v1/runs/{RUN_ID}/metrics/summary",
            headers=OWNER_HEADERS,
        )
    assert resp.status == 404


# ---------------------------------------------------------------------------
# GET /api/v1/runs/{run_id}/metrics/aggregations
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_metrics_aggregations(client: TestClient) -> None:
    """GET aggregations returns bucketed series."""
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.get(
            f"/api/v1/runs/{RUN_ID}/metrics/aggregations"
            "?names=loss&from_step=0&to_step=100&bucket_size=10",
            headers=OWNER_HEADERS,
        )
    assert resp.status == 200
    body = await resp.json()
    assert body["run_id"] == str(RUN_ID)
    assert body["bucket_size"] == 10
    assert "series" in body
    svc.get_aggregations.assert_awaited_once()
    call_kwargs = svc.get_aggregations.call_args.kwargs
    assert call_kwargs["names"] == ["loss"]
    assert call_kwargs["from_step"] == 0
    assert call_kwargs["to_step"] == 100
    assert call_kwargs["bucket_size"] == 10


@pytest.mark.asyncio
async def test_metrics_aggregations_requires_names(client: TestClient) -> None:
    """GET aggregations without names returns 400."""
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.get(
            f"/api/v1/runs/{RUN_ID}/metrics/aggregations",
            headers=OWNER_HEADERS,
        )
    assert resp.status == 400


@pytest.mark.asyncio
async def test_metrics_aggregations_invalid_bucket_size(client: TestClient) -> None:
    """GET aggregations with bucket_size=0 returns 400."""
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.get(
            f"/api/v1/runs/{RUN_ID}/metrics/aggregations?names=loss&bucket_size=0",
            headers=OWNER_HEADERS,
        )
    assert resp.status == 400


# ---------------------------------------------------------------------------
# RBAC
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_metrics_rbac_viewer_can_read(client: TestClient) -> None:
    """Viewer (experiments.view) can read metrics, summary and aggregations."""
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.get(
            f"/api/v1/runs/{RUN_ID}/metrics",
            headers=VIEWER_HEADERS,
        )
        assert resp.status == 200

        resp = await client.get(
            f"/api/v1/runs/{RUN_ID}/metrics/summary",
            headers=VIEWER_HEADERS,
        )
        assert resp.status == 200

        resp = await client.get(
            f"/api/v1/runs/{RUN_ID}/metrics/aggregations?names=loss",
            headers=VIEWER_HEADERS,
        )
        assert resp.status == 200


@pytest.mark.asyncio
async def test_metrics_rbac_viewer_cannot_write(client: TestClient) -> None:
    """Viewer without metrics.write permission cannot POST metrics."""
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.post(
            f"/api/v1/runs/{RUN_ID}/metrics",
            json={
                "metrics": [
                    {"name": "loss", "step": 1, "value": 0.5, "timestamp": "2025-01-01T00:00:00Z"}
                ]
            },
            headers=VIEWER_HEADERS,
        )
    assert resp.status == 403


@pytest.mark.asyncio
async def test_metrics_rbac_no_auth(client: TestClient) -> None:
    """Requests without X-User-Id header return 401."""
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.get(f"/api/v1/runs/{RUN_ID}/metrics")
    assert resp.status == 401


# ---------------------------------------------------------------------------
# MetricsService unit tests (no HTTP layer)
# ---------------------------------------------------------------------------

@pytest.mark.asyncio
async def test_service_ingest_batch_limit() -> None:
    """Service raises ValueError when batch exceeds BATCH_LIMIT."""
    from experiment_service.domain.dto import RunMetricPointDTO

    mock_run_repo = MagicMock()
    mock_metrics_repo = MagicMock()
    mock_run_repo.get = AsyncMock(return_value=MagicMock())
    mock_metrics_repo.bulk_insert = AsyncMock()

    svc = MetricsService(mock_run_repo, mock_metrics_repo)
    oversized = [
        RunMetricPointDTO(name="loss", step=i, value=float(i), timestamp=_TS)
        for i in range(MetricsService.BATCH_LIMIT + 1)
    ]
    with pytest.raises(ValueError, match="Batch too large"):
        await svc.ingest_metrics(PROJECT_ID, RUN_ID, oversized)


@pytest.mark.asyncio
async def test_service_query_metrics_pagination() -> None:
    """Service passes limit/offset to repository and includes them in response."""
    from experiment_service.domain.dto import RunMetricPointDTO

    mock_run_repo = MagicMock()
    mock_metrics_repo = MagicMock()
    mock_run_repo.get = AsyncMock(return_value=MagicMock())
    mock_metrics_repo.count_series = AsyncMock(return_value=50)
    mock_metrics_repo.fetch_series = AsyncMock(
        return_value=[_make_run_metric("loss", 1, 0.5)]
    )

    svc = MetricsService(mock_run_repo, mock_metrics_repo)
    result = await svc.query_metrics(PROJECT_ID, RUN_ID, limit=5, offset=10)

    assert result["total"] == 50
    assert result["limit"] == 5
    assert result["offset"] == 10
    mock_metrics_repo.fetch_series.assert_awaited_once()
    call_kwargs = mock_metrics_repo.fetch_series.call_args.kwargs
    assert call_kwargs["limit"] == 5
    assert call_kwargs["offset"] == 10


@pytest.mark.asyncio
async def test_service_get_summary_merges_aggregates_and_last() -> None:
    """Service correctly merges aggregate rows with last-value rows."""
    from unittest.mock import MagicMock

    mock_run_repo = MagicMock()
    mock_metrics_repo = MagicMock()
    mock_run_repo.get = AsyncMock(return_value=MagicMock())

    agg_row: dict[str, Any] = {
        "name": "loss",
        "total_steps": 3,
        "min_value": 0.1,
        "max_value": 0.9,
        "avg_value": 0.5,
    }
    last_row: dict[str, Any] = {
        "name": "loss",
        "last_step": 3,
        "last_value": 0.1,
        "last_timestamp": _TS,
    }

    # asyncpg Record-like: support dict-style access
    def _make_record(d: dict) -> MagicMock:
        rec = MagicMock()
        rec.__getitem__ = lambda self, key: d[key]
        return rec

    mock_metrics_repo.fetch_summary_aggregates = AsyncMock(
        return_value=[_make_record(agg_row)]
    )
    mock_metrics_repo.fetch_last_per_metric = AsyncMock(
        return_value=[_make_record(last_row)]
    )

    svc = MetricsService(mock_run_repo, mock_metrics_repo)
    result = await svc.get_summary(PROJECT_ID, RUN_ID, names=["loss"])

    assert result["run_id"] == str(RUN_ID)
    assert len(result["metrics"]) == 1
    m = result["metrics"][0]
    assert m["name"] == "loss"
    assert m["last_step"] == 3
    assert m["total_steps"] == 3
    assert abs(m["min_value"] - 0.1) < 1e-9


@pytest.mark.asyncio
async def test_service_get_aggregations_auto_bucket() -> None:
    """Service calculates bucket_size automatically when not provided."""
    mock_run_repo = MagicMock()
    mock_metrics_repo = MagicMock()
    mock_run_repo.get = AsyncMock(return_value=MagicMock())
    mock_metrics_repo.fetch_aggregations = AsyncMock(return_value=[])

    svc = MetricsService(mock_run_repo, mock_metrics_repo)
    result = await svc.get_aggregations(
        PROJECT_ID, RUN_ID, names=["loss"], from_step=0, to_step=3000
    )
    # Auto: (3000 - 0) // 300 = 10
    assert result["bucket_size"] == 10
    call_kwargs = mock_metrics_repo.fetch_aggregations.call_args.kwargs
    assert call_kwargs["bucket_size"] == 10
