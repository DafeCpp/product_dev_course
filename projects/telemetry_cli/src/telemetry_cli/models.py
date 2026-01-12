from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Any


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def to_rfc3339_z(dt: datetime) -> str:
    # Ingest API expects "format: date-time". In tests they use "...Z".
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)
    dt = dt.astimezone(timezone.utc)
    return dt.isoformat().replace("+00:00", "Z")


@dataclass(frozen=True, slots=True)
class TelemetryReading:
    """One time-series reading for Telemetry Ingest Service."""

    timestamp: datetime
    raw_value: float
    signal: str
    physical_value: float | None = None
    meta: dict[str, Any] = field(default_factory=dict)

    def as_ingest_dict(self) -> dict[str, Any]:
        payload: dict[str, Any] = {
            "timestamp": to_rfc3339_z(self.timestamp),
            "raw_value": self.raw_value,
            "meta": {"signal": self.signal, **self.meta},
        }
        if self.physical_value is not None:
            payload["physical_value"] = self.physical_value
        return payload


