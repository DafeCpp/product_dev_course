from __future__ import annotations

import json
import sys
from datetime import datetime, timezone
from typing import Any

import httpx


class ETPClient:
    """Synchronous HTTP client for the Experiment Service (CI/CD usage)."""

    def __init__(self, *, base_url: str, token: str, timeout_s: float = 30.0) -> None:
        self._base_url = base_url.rstrip("/")
        self._headers = {"Authorization": f"Bearer {token}", "Content-Type": "application/json"}
        self._timeout = timeout_s

    # ------------------------------------------------------------------
    # runs
    # ------------------------------------------------------------------

    def run_create(
        self,
        *,
        experiment_id: str,
        project_id: str,
        created_by: str,
        name: str | None = None,
        params: dict[str, Any] | None = None,
        notes: str | None = None,
        git_sha: str | None = None,
        env: str | None = None,
    ) -> dict[str, Any]:
        body: dict[str, Any] = {
            "experiment_id": experiment_id,
            "project_id": project_id,
            "created_by": created_by,
        }
        if name is not None:
            body["name"] = name
        if params is not None:
            body["params"] = params
        if notes is not None:
            body["notes"] = notes
        if git_sha is not None:
            body["git_sha"] = git_sha
        if env is not None:
            body["env"] = env

        resp = httpx.post(
            f"{self._base_url}/api/v1/experiments/{experiment_id}/runs",
            json=body,
            headers=self._headers,
            timeout=self._timeout,
        )
        resp.raise_for_status()
        return resp.json()  # type: ignore[no-any-return]

    def run_finish(self, *, run_id: str, status: str) -> dict[str, Any]:
        body: dict[str, Any] = {"status": status}
        resp = httpx.patch(
            f"{self._base_url}/api/v1/runs/{run_id}",
            json=body,
            headers=self._headers,
            timeout=self._timeout,
        )
        resp.raise_for_status()
        return resp.json()  # type: ignore[no-any-return]

    # ------------------------------------------------------------------
    # metrics
    # ------------------------------------------------------------------

    def metrics_push(self, *, run_id: str, metrics: list[dict[str, Any]]) -> None:
        # Inject current timestamp for any metric missing it
        now = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
        for m in metrics:
            m.setdefault("timestamp", now)

        body: dict[str, Any] = {"metrics": metrics}
        resp = httpx.post(
            f"{self._base_url}/api/v1/runs/{run_id}/metrics",
            json=body,
            headers=self._headers,
            timeout=self._timeout,
        )
        resp.raise_for_status()

    # ------------------------------------------------------------------
    # artifacts
    # ------------------------------------------------------------------

    def artifact_register(
        self,
        *,
        run_id: str,
        artifact_type: str,
        uri: str,
        checksum: str | None = None,
        size: int | None = None,
    ) -> dict[str, Any]:
        body: dict[str, Any] = {"type": artifact_type, "uri": uri}
        if checksum is not None:
            body["checksum"] = checksum
        if size is not None:
            body["size"] = size

        resp = httpx.post(
            f"{self._base_url}/api/v1/runs/{run_id}/artifacts",
            json=body,
            headers=self._headers,
            timeout=self._timeout,
        )
        resp.raise_for_status()
        return resp.json()  # type: ignore[no-any-return]


def load_metrics_json(raw: str) -> list[dict[str, Any]]:
    """Parse raw JSON — accepts either {"metrics": [...]} or plain [...]."""
    data: Any = json.loads(raw)
    if isinstance(data, list):
        return data  # type: ignore[return-value]
    if isinstance(data, dict) and "metrics" in data:
        return data["metrics"]  # type: ignore[return-value]
    print("ERROR: JSON must be a list of metric objects or {\"metrics\": [...]}", file=sys.stderr)
    sys.exit(1)
