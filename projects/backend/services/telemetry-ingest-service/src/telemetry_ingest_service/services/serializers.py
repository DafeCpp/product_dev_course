"""JSON-safe serializers for telemetry read models."""
from __future__ import annotations

import json
from datetime import datetime, timezone
from typing import Any


def _timestamp(value: object) -> str:
    if isinstance(value, datetime):
        return value.astimezone(timezone.utc).isoformat().replace("+00:00", "Z")
    return str(value)


def serialize_telemetry_record(row: dict[str, Any]) -> dict[str, Any]:
    meta = row["meta"]
    if isinstance(meta, str):
        meta = json.loads(meta)
    return {
        "id": int(row["id"]), "project_id": str(row["project_id"]) if "project_id" in row else None,
        "sensor_id": str(row["sensor_id"]) if "sensor_id" in row else None,
        "timestamp": _timestamp(row["timestamp"]), "raw_value": row["raw_value"],
        "physical_value": row["physical_value"], "run_id": str(row["run_id"]) if row.get("run_id") else None,
        "capture_session_id": str(row["capture_session_id"]) if row.get("capture_session_id") else None,
        "meta": meta,
    }


def serialize_aggregated_record(row: dict[str, Any]) -> dict[str, Any]:
    return {
        "bucket": _timestamp(row["bucket"]), "sensor_id": str(row["sensor_id"]) if row.get("sensor_id") else None,
        "signal": row.get("signal"), "capture_session_id": str(row["capture_session_id"]) if row.get("capture_session_id") else None,
        "sample_count": int(row["sample_count"]), "avg_raw": row["avg_raw"], "min_raw": row["min_raw"],
        "max_raw": row["max_raw"], "avg_physical": row["avg_physical"],
        "min_physical": row["min_physical"], "max_physical": row["max_physical"],
    }
