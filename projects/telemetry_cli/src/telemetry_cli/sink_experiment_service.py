from __future__ import annotations

from typing import Any
from uuid import UUID

import httpx

from telemetry_cli.models import TelemetryReading


class ExperimentServiceClient:
    def __init__(self, *, base_url: str, sensor_token: str, timeout_s: float = 10.0):
        self._base_url = base_url.rstrip("/")
        self._token = sensor_token
        self._timeout_s = timeout_s
        self._client: httpx.AsyncClient | None = None

    async def __aenter__(self) -> "ExperimentServiceClient":
        self._client = httpx.AsyncClient(timeout=self._timeout_s)
        return self

    async def __aexit__(self, exc_type, exc, tb) -> None:
        if self._client:
            await self._client.aclose()
            self._client = None

    async def ingest(
        self,
        *,
        sensor_id: UUID,
        run_id: UUID | None,
        capture_session_id: UUID | None,
        meta: dict[str, Any],
        readings: list[TelemetryReading],
    ) -> dict[str, Any]:
        if not self._client:
            raise RuntimeError("Client is not started; use 'async with ExperimentServiceClient(...)'.")
        if not readings:
            return {"status": "skipped", "accepted": 0}

        payload: dict[str, Any] = {
            "sensor_id": str(sensor_id),
            "readings": [],
        }
        for r in readings:
            item = r.as_ingest_dict()
            # Merge global meta into each reading's meta (API forbids top-level `meta`)
            if meta:
                item_meta = item.get("meta") or {}
                item["meta"] = {**meta, **item_meta}
            payload["readings"].append(item)
        if run_id is not None:
            payload["run_id"] = str(run_id)
        if capture_session_id is not None:
            payload["capture_session_id"] = str(capture_session_id)

        resp = await self._client.post(
            f"{self._base_url}/api/v1/telemetry",
            json=payload,
            headers={"Authorization": f"Bearer {self._token}"},
        )
        resp.raise_for_status()
        return resp.json()


