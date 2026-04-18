"""Disk spool: write-ahead buffer for telemetry ingest when DB writes fail.

Each failed ingest call creates one JSON file in the spool directory.
A background flush worker replays the files in chronological (FIFO) order
once the DB becomes available again.

File naming: ``{unix_ns:020d}_{uuid4_hex}.json``
Sorting by name gives FIFO order; the uuid4 suffix avoids collisions when
two calls land in the same nanosecond.

The spool file stores *fully-prepared insertion rows* (with conversion
profiles already applied and meta already merged).  This means the flush
worker only needs to execute ``executemany`` without re-doing any business
logic, keeping replay simple and deterministic.
"""
from __future__ import annotations

import json
import time
import structlog
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from uuid import UUID, uuid4

from telemetry_ingest_service.settings import settings

logger = structlog.get_logger(__name__)

_SUFFIX = ".json"
_VERSION = 1


@dataclass
class SpoolRecord:
    """Fully-prepared data for an INSERT replay + sensor heartbeat update."""

    sensor_id: UUID
    # Each element mirrors one ``telemetry_records`` row as a JSON-safe dict.
    items: list[dict[str, Any]]
    # ISO-format timestamp of the latest reading — used for the heartbeat UPDATE.
    last_reading_ts: str
    spooled_at: datetime = field(
        default_factory=lambda: datetime.now(timezone.utc)
    )


# ---------------------------------------------------------------------------
# I/O helpers
# ---------------------------------------------------------------------------


def _dir() -> Path:
    d = Path(settings.spool_dir)
    d.mkdir(parents=True, exist_ok=True)
    return d


def write_spool(record: SpoolRecord) -> Path:
    """Atomically write *record* to the spool directory.

    Uses a write-then-rename strategy so readers never observe partial files.
    Raises ``RuntimeError`` if the directory has reached ``spool_max_files``.
    """
    d = _dir()
    existing = list(d.glob(f"*{_SUFFIX}"))
    if len(existing) >= settings.spool_max_files:
        raise RuntimeError(
            f"Spool is full ({len(existing)}/{settings.spool_max_files} files). "
            "Telemetry batch dropped."
        )

    name = f"{time.time_ns():020d}_{uuid4().hex}{_SUFFIX}"
    path = d / name
    tmp = path.with_suffix(".tmp")

    payload = {
        "version": _VERSION,
        "spooled_at": record.spooled_at.isoformat(),
        "sensor_id": str(record.sensor_id),
        "last_reading_ts": record.last_reading_ts,
        "items": record.items,
    }
    tmp.write_text(json.dumps(payload), encoding="utf-8")
    tmp.rename(path)  # atomic rename on the same filesystem

    logger.info("spool_write", path=str(path), items=len(record.items))
    return path


def list_spool_files() -> list[Path]:
    """Return spool files sorted oldest-first (FIFO replay order)."""
    return sorted(_dir().glob(f"*{_SUFFIX}"))


def read_spool(path: Path) -> SpoolRecord:
    data = json.loads(path.read_text(encoding="utf-8"))
    return SpoolRecord(
        sensor_id=UUID(data["sensor_id"]),
        items=data["items"],
        last_reading_ts=data["last_reading_ts"],
        spooled_at=datetime.fromisoformat(data["spooled_at"]),
    )


def delete_spool(path: Path) -> None:
    try:
        path.unlink()
    except FileNotFoundError:
        pass
