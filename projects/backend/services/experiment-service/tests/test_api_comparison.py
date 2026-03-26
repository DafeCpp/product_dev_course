"""Unit tests for Comparison Service (mock-based, no DB required)."""
from __future__ import annotations

# pyright: reportMissingImports=false

import uuid
from typing import Any
from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from aiohttp import web
from aiohttp.test_utils import TestClient

from experiment_service.api.routes.comparison import routes
from experiment_service.core.exceptions import NotFoundError
from experiment_service.services.metrics import MetricsService, MAX_COMPARISON_RUNS, MAX_COMPARISON_METRICS

# ---------------------------------------------------------------------------
# Constants / shared fixtures
# ---------------------------------------------------------------------------

PROJECT_ID = uuid.uuid4()
EXPERIMENT_ID = uuid.uuid4()
RUN_ID_1 = uuid.uuid4()
RUN_ID_2 = uuid.uuid4()
RUN_ID_3 = uuid.uuid4()

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


def _make_comparison_response(
    experiment_id: uuid.UUID = EXPERIMENT_ID,
    run_ids: list[uuid.UUID] | None = None,
) -> dict:
    if run_ids is None:
        run_ids = [RUN_ID_1, RUN_ID_2]
    return {
        "experiment_id": str(experiment_id),
        "metric_names": ["loss"],
        "runs": [
            {
                "run_id": str(rid),
                "run_name": f"run-{i}",
                "status": "completed",
                "metrics": {
                    "loss": {
                        "summary": {
                            "last_value": 0.1,
                            "last_step": 100,
                            "min": 0.05,
                            "max": 1.0,
                            "avg": 0.5,
                            "count": 100,
                        },
                        "series": [{"step": 0, "value": 1.0}, {"step": 100, "value": 0.1}],
                    }
                },
            }
            for i, rid in enumerate(run_ids)
        ],
    }


def _make_mock_service(
    *,
    compare_return: dict | None = None,
    raise_not_found: bool = False,
    raise_value_error: str | None = None,
) -> MetricsService:
    mock_run_repo = MagicMock()
    mock_metrics_repo = MagicMock()
    svc = MetricsService(mock_run_repo, mock_metrics_repo)

    if raise_not_found:
        svc.compare_runs = AsyncMock(side_effect=NotFoundError("Run not found"))  # type: ignore[method-assign]
    elif raise_value_error:
        svc.compare_runs = AsyncMock(side_effect=ValueError(raise_value_error))  # type: ignore[method-assign]
    else:
        if compare_return is None:
            compare_return = _make_comparison_response()
        svc.compare_runs = AsyncMock(return_value=compare_return)  # type: ignore[method-assign]

    return svc


def _patch_service(mock_svc: MetricsService):
    return patch(
        "experiment_service.api.routes.comparison.get_metrics_service",
        new=AsyncMock(return_value=mock_svc),
    )


@pytest.fixture
async def client(aiohttp_client: Any) -> TestClient:
    app = web.Application()
    app.add_routes(routes)
    return await aiohttp_client(app)


# ---------------------------------------------------------------------------
# POST endpoint
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_compare_post_endpoint(client: TestClient) -> None:
    """POST returns 200 with comparison payload."""
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.post(
            f"/api/v1/experiments/{EXPERIMENT_ID}/compare",
            json={
                "run_ids": [str(RUN_ID_1), str(RUN_ID_2)],
                "metric_names": ["loss"],
            },
            headers=OWNER_HEADERS,
        )
    assert resp.status == 200
    body = await resp.json()
    assert "experiment_id" in body
    assert "runs" in body
    assert "metric_names" in body
    svc.compare_runs.assert_awaited_once()


@pytest.mark.asyncio
async def test_compare_post_passes_params(client: TestClient) -> None:
    """POST forwards from_step, to_step, max_points_per_series to service."""
    svc = _make_mock_service()
    with _patch_service(svc):
        await client.post(
            f"/api/v1/experiments/{EXPERIMENT_ID}/compare",
            json={
                "run_ids": [str(RUN_ID_1), str(RUN_ID_2)],
                "metric_names": ["loss", "accuracy"],
                "from_step": 10,
                "to_step": 500,
                "max_points_per_series": 200,
            },
            headers=OWNER_HEADERS,
        )
    kwargs = svc.compare_runs.call_args.kwargs
    assert kwargs["from_step"] == 10
    assert kwargs["to_step"] == 500
    assert kwargs["max_points_per_series"] == 200
    assert kwargs["metric_names"] == ["loss", "accuracy"]


@pytest.mark.asyncio
async def test_compare_post_missing_run_ids(client: TestClient) -> None:
    """POST without run_ids returns 400."""
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.post(
            f"/api/v1/experiments/{EXPERIMENT_ID}/compare",
            json={"metric_names": ["loss"]},
            headers=OWNER_HEADERS,
        )
    assert resp.status == 400


@pytest.mark.asyncio
async def test_compare_post_missing_metric_names(client: TestClient) -> None:
    """POST without metric_names returns 400."""
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.post(
            f"/api/v1/experiments/{EXPERIMENT_ID}/compare",
            json={"run_ids": [str(RUN_ID_1), str(RUN_ID_2)]},
            headers=OWNER_HEADERS,
        )
    assert resp.status == 400


# ---------------------------------------------------------------------------
# GET endpoint
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_compare_get_endpoint(client: TestClient) -> None:
    """GET returns 200 with comparison payload."""
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.get(
            f"/api/v1/experiments/{EXPERIMENT_ID}/compare"
            f"?run_ids={RUN_ID_1},{RUN_ID_2}&names=loss",
            headers=OWNER_HEADERS,
        )
    assert resp.status == 200
    body = await resp.json()
    assert "runs" in body
    svc.compare_runs.assert_awaited_once()


@pytest.mark.asyncio
async def test_compare_get_missing_run_ids(client: TestClient) -> None:
    """GET without run_ids returns 400."""
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.get(
            f"/api/v1/experiments/{EXPERIMENT_ID}/compare?names=loss",
            headers=OWNER_HEADERS,
        )
    assert resp.status == 400


@pytest.mark.asyncio
async def test_compare_get_missing_names(client: TestClient) -> None:
    """GET without names returns 400."""
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.get(
            f"/api/v1/experiments/{EXPERIMENT_ID}/compare?run_ids={RUN_ID_1},{RUN_ID_2}",
            headers=OWNER_HEADERS,
        )
    assert resp.status == 400


@pytest.mark.asyncio
async def test_compare_get_passes_step_params(client: TestClient) -> None:
    """GET forwards from_step, to_step, max_points to service."""
    svc = _make_mock_service()
    with _patch_service(svc):
        await client.get(
            f"/api/v1/experiments/{EXPERIMENT_ID}/compare"
            f"?run_ids={RUN_ID_1},{RUN_ID_2}&names=loss"
            "&from_step=5&to_step=200&max_points=300",
            headers=OWNER_HEADERS,
        )
    kwargs = svc.compare_runs.call_args.kwargs
    assert kwargs["from_step"] == 5
    assert kwargs["to_step"] == 200
    assert kwargs["max_points_per_series"] == 300


# ---------------------------------------------------------------------------
# RBAC
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_compare_rbac_viewer_can_access(client: TestClient) -> None:
    """Viewer (experiments.view) can use both GET and POST compare endpoints."""
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.get(
            f"/api/v1/experiments/{EXPERIMENT_ID}/compare"
            f"?run_ids={RUN_ID_1},{RUN_ID_2}&names=loss",
            headers=VIEWER_HEADERS,
        )
        assert resp.status == 200

        resp = await client.post(
            f"/api/v1/experiments/{EXPERIMENT_ID}/compare",
            json={
                "run_ids": [str(RUN_ID_1), str(RUN_ID_2)],
                "metric_names": ["loss"],
            },
            headers=VIEWER_HEADERS,
        )
        assert resp.status == 200


@pytest.mark.asyncio
async def test_compare_rbac_unauthenticated(client: TestClient) -> None:
    """Requests without X-User-Id header return 401."""
    svc = _make_mock_service()
    with _patch_service(svc):
        resp = await client.get(
            f"/api/v1/experiments/{EXPERIMENT_ID}/compare"
            f"?run_ids={RUN_ID_1},{RUN_ID_2}&names=loss"
        )
        assert resp.status == 401

        resp = await client.post(
            f"/api/v1/experiments/{EXPERIMENT_ID}/compare",
            json={
                "run_ids": [str(RUN_ID_1), str(RUN_ID_2)],
                "metric_names": ["loss"],
            },
        )
        assert resp.status == 401


# ---------------------------------------------------------------------------
# Service-level unit tests
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_compare_returns_runs_with_metrics() -> None:
    """compare_runs assembles correct response structure."""
    mock_run_repo = MagicMock()
    mock_metrics_repo = MagicMock()

    mock_run_repo.fetch_runs_brief = AsyncMock(
        return_value=[
            {"id": RUN_ID_1, "name": "baseline", "status": "completed", "experiment_id": EXPERIMENT_ID},
            {"id": RUN_ID_2, "name": "exp-v2", "status": "completed", "experiment_id": EXPERIMENT_ID},
        ]
    )

    def _make_record(d: dict) -> MagicMock:
        rec = MagicMock()
        rec.__getitem__ = lambda self, key: d[key]
        return rec

    mock_metrics_repo.fetch_multi_run_summary = AsyncMock(return_value=[
        _make_record({"run_id": RUN_ID_1, "name": "loss", "total_steps": 100, "min_value": 0.05, "max_value": 1.0, "avg_value": 0.5}),
        _make_record({"run_id": RUN_ID_2, "name": "loss", "total_steps": 50, "min_value": 0.1, "max_value": 0.9, "avg_value": 0.4}),
    ])
    mock_metrics_repo.fetch_multi_run_last = AsyncMock(return_value=[
        _make_record({"run_id": RUN_ID_1, "name": "loss", "last_step": 100, "last_value": 0.05}),
        _make_record({"run_id": RUN_ID_2, "name": "loss", "last_step": 50, "last_value": 0.1}),
    ])
    mock_metrics_repo.count_multi_run_points = AsyncMock(return_value=[
        _make_record({"run_id": RUN_ID_1, "name": "loss", "cnt": 100}),
        _make_record({"run_id": RUN_ID_2, "name": "loss", "cnt": 50}),
    ])
    mock_metrics_repo.fetch_multi_run_series = AsyncMock(return_value=[
        _make_record({"run_id": RUN_ID_1, "name": "loss", "step": 0, "value": 1.0}),
        _make_record({"run_id": RUN_ID_1, "name": "loss", "step": 100, "value": 0.05}),
        _make_record({"run_id": RUN_ID_2, "name": "loss", "step": 0, "value": 0.9}),
        _make_record({"run_id": RUN_ID_2, "name": "loss", "step": 50, "value": 0.1}),
    ])

    svc = MetricsService(mock_run_repo, mock_metrics_repo)
    result = await svc.compare_runs(
        PROJECT_ID, EXPERIMENT_ID,
        run_ids=[RUN_ID_1, RUN_ID_2],
        metric_names=["loss"],
    )

    assert result["experiment_id"] == str(EXPERIMENT_ID)
    assert result["metric_names"] == ["loss"]
    assert len(result["runs"]) == 2
    run1 = next(r for r in result["runs"] if r["run_id"] == str(RUN_ID_1))
    assert "loss" in run1["metrics"]
    assert run1["metrics"]["loss"]["summary"]["count"] == 100
    assert len(run1["metrics"]["loss"]["series"]) == 2


@pytest.mark.asyncio
async def test_compare_validates_max_runs() -> None:
    """compare_runs raises ValueError when run_ids > MAX_COMPARISON_RUNS."""
    mock_run_repo = MagicMock()
    mock_metrics_repo = MagicMock()
    svc = MetricsService(mock_run_repo, mock_metrics_repo)

    too_many = [uuid.uuid4() for _ in range(MAX_COMPARISON_RUNS + 1)]
    with pytest.raises(ValueError, match="run_ids must not exceed"):
        await svc.compare_runs(
            PROJECT_ID, EXPERIMENT_ID,
            run_ids=too_many,
            metric_names=["loss"],
        )


@pytest.mark.asyncio
async def test_compare_validates_max_metrics() -> None:
    """compare_runs raises ValueError when metric_names > MAX_COMPARISON_METRICS."""
    mock_run_repo = MagicMock()
    mock_metrics_repo = MagicMock()
    svc = MetricsService(mock_run_repo, mock_metrics_repo)

    too_many_names = [f"metric_{i}" for i in range(MAX_COMPARISON_METRICS + 1)]
    with pytest.raises(ValueError, match="metric_names must not exceed"):
        await svc.compare_runs(
            PROJECT_ID, EXPERIMENT_ID,
            run_ids=[uuid.uuid4(), uuid.uuid4()],
            metric_names=too_many_names,
        )


@pytest.mark.asyncio
async def test_compare_validates_min_runs() -> None:
    """compare_runs raises ValueError when run_ids < 2."""
    mock_run_repo = MagicMock()
    mock_metrics_repo = MagicMock()
    svc = MetricsService(mock_run_repo, mock_metrics_repo)

    with pytest.raises(ValueError, match="at least 2"):
        await svc.compare_runs(
            PROJECT_ID, EXPERIMENT_ID,
            run_ids=[uuid.uuid4()],
            metric_names=["loss"],
        )


@pytest.mark.asyncio
async def test_compare_run_not_in_experiment() -> None:
    """compare_runs raises NotFoundError when run doesn't belong to experiment."""
    mock_run_repo = MagicMock()
    mock_metrics_repo = MagicMock()

    # fetch_runs_brief returns only one run — the other is not in this experiment
    mock_run_repo.fetch_runs_brief = AsyncMock(
        return_value=[
            {"id": RUN_ID_1, "name": "r1", "status": "completed", "experiment_id": EXPERIMENT_ID},
        ]
    )

    svc = MetricsService(mock_run_repo, mock_metrics_repo)
    with pytest.raises(NotFoundError, match="not in experiment"):
        await svc.compare_runs(
            PROJECT_ID, EXPERIMENT_ID,
            run_ids=[RUN_ID_1, RUN_ID_2],
            metric_names=["loss"],
        )


@pytest.mark.asyncio
async def test_compare_downsamples_large_series() -> None:
    """compare_runs uses bucketed query when max count > max_points_per_series."""
    mock_run_repo = MagicMock()
    mock_metrics_repo = MagicMock()

    mock_run_repo.fetch_runs_brief = AsyncMock(
        return_value=[
            {"id": RUN_ID_1, "name": "r1", "status": "running", "experiment_id": EXPERIMENT_ID},
            {"id": RUN_ID_2, "name": "r2", "status": "running", "experiment_id": EXPERIMENT_ID},
        ]
    )

    def _rec(d: dict) -> MagicMock:
        rec = MagicMock()
        rec.__getitem__ = lambda self, key: d[key]
        return rec

    mock_metrics_repo.fetch_multi_run_summary = AsyncMock(return_value=[])
    mock_metrics_repo.fetch_multi_run_last = AsyncMock(return_value=[])
    # 2000 points — exceeds default max_points_per_series=500
    mock_metrics_repo.count_multi_run_points = AsyncMock(return_value=[
        _rec({"run_id": RUN_ID_1, "name": "loss", "cnt": 2000}),
    ])
    mock_metrics_repo.fetch_multi_run_series_bucketed = AsyncMock(return_value=[])

    svc = MetricsService(mock_run_repo, mock_metrics_repo)
    await svc.compare_runs(
        PROJECT_ID, EXPERIMENT_ID,
        run_ids=[RUN_ID_1, RUN_ID_2],
        metric_names=["loss"],
        max_points_per_series=500,
    )

    mock_metrics_repo.fetch_multi_run_series_bucketed.assert_awaited_once()
    # fetch_multi_run_series (raw) should NOT have been called
    mock_metrics_repo.fetch_multi_run_series = AsyncMock()
    mock_metrics_repo.fetch_multi_run_series.assert_not_called()

    call_kwargs = mock_metrics_repo.fetch_multi_run_series_bucketed.call_args.kwargs
    # bucket_size = max(1, 2000 // 500) = 4
    assert call_kwargs["bucket_size"] == 4


@pytest.mark.asyncio
async def test_compare_no_downsampling_when_within_limit() -> None:
    """compare_runs uses raw series when count <= max_points_per_series."""
    mock_run_repo = MagicMock()
    mock_metrics_repo = MagicMock()

    mock_run_repo.fetch_runs_brief = AsyncMock(
        return_value=[
            {"id": RUN_ID_1, "name": "r1", "status": "running", "experiment_id": EXPERIMENT_ID},
            {"id": RUN_ID_2, "name": "r2", "status": "running", "experiment_id": EXPERIMENT_ID},
        ]
    )

    def _rec(d: dict) -> MagicMock:
        rec = MagicMock()
        rec.__getitem__ = lambda self, key: d[key]
        return rec

    mock_metrics_repo.fetch_multi_run_summary = AsyncMock(return_value=[])
    mock_metrics_repo.fetch_multi_run_last = AsyncMock(return_value=[])
    mock_metrics_repo.count_multi_run_points = AsyncMock(return_value=[
        _rec({"run_id": RUN_ID_1, "name": "loss", "cnt": 100}),
    ])
    mock_metrics_repo.fetch_multi_run_series = AsyncMock(return_value=[])

    svc = MetricsService(mock_run_repo, mock_metrics_repo)
    await svc.compare_runs(
        PROJECT_ID, EXPERIMENT_ID,
        run_ids=[RUN_ID_1, RUN_ID_2],
        metric_names=["loss"],
        max_points_per_series=500,
    )

    mock_metrics_repo.fetch_multi_run_series.assert_awaited_once()


@pytest.mark.asyncio
async def test_compare_post_returns_404_on_not_found(client: TestClient) -> None:
    """POST returns 404 when service raises NotFoundError."""
    svc = _make_mock_service(raise_not_found=True)
    with _patch_service(svc):
        resp = await client.post(
            f"/api/v1/experiments/{EXPERIMENT_ID}/compare",
            json={
                "run_ids": [str(RUN_ID_1), str(RUN_ID_2)],
                "metric_names": ["loss"],
            },
            headers=OWNER_HEADERS,
        )
    assert resp.status == 404


@pytest.mark.asyncio
async def test_compare_post_returns_400_on_validation_error(client: TestClient) -> None:
    """POST returns 400 when service raises ValueError."""
    svc = _make_mock_service(raise_value_error="run_ids must not exceed 10 runs")
    with _patch_service(svc):
        resp = await client.post(
            f"/api/v1/experiments/{EXPERIMENT_ID}/compare",
            json={
                "run_ids": [str(uuid.uuid4()) for _ in range(11)],
                "metric_names": ["loss"],
            },
            headers=OWNER_HEADERS,
        )
    assert resp.status == 400
