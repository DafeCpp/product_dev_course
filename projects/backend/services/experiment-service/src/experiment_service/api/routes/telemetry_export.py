"""Telemetry data export endpoints (CSV / JSON)."""
from __future__ import annotations

import csv
import io
import json
from uuid import UUID

from aiohttp import web

from backend_common.db.pool import get_pool_service as get_pool
from experiment_service.api.utils import parse_uuid
from experiment_service.services.dependencies import (
    ensure_project_access,
    get_capture_session_service,
    get_run_service,
    require_current_user,
    resolve_project_id,
)

routes = web.RouteTableDef()

TELEMETRY_EXPORT_LIMIT = 100_000  # max telemetry rows per export


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------


def _parse_value_mode(raw: str | None) -> str:
    mode = (raw or "both").lower()
    if mode not in ("raw", "physical", "both"):
        raise web.HTTPBadRequest(text="raw_or_physical must be raw, physical, or both")
    return mode


async def _fetch_telemetry_rows(
    pool,
    capture_session_ids: list[UUID],
    *,
    sensor_id: UUID | None = None,
    signal: str | None = None,
    include_late: bool = True,
    limit: int = TELEMETRY_EXPORT_LIMIT,
) -> tuple[list, bool]:
    """Fetch telemetry rows for given capture sessions.

    Returns (rows, truncated).
    """
    conditions = ["capture_session_id = ANY($1::uuid[])"]
    params: list = [capture_session_ids]
    idx = 2

    if not include_late:
        conditions.append(
            "NOT coalesce((meta->'__system'->>'late')::boolean, false)"
        )

    if sensor_id is not None:
        conditions.append(f"sensor_id = ${idx}")
        params.append(sensor_id)
        idx += 1

    if signal is not None:
        conditions.append(f"signal = ${idx}")
        params.append(signal)
        idx += 1

    where = " AND ".join(conditions)
    query = f"""
        SELECT
            timestamp, sensor_id, signal,
            raw_value, physical_value,
            conversion_status, capture_session_id
        FROM telemetry_records
        WHERE {where}
        ORDER BY timestamp ASC
        LIMIT {limit + 1}
    """
    async with pool.acquire() as conn:
        rows = await conn.fetch(query, *params)

    truncated = len(rows) > limit
    if truncated:
        rows = rows[:limit]
    return rows, truncated


async def _fetch_telemetry_aggregated(
    pool,
    capture_session_ids: list[UUID],
    *,
    sensor_id: UUID | None = None,
    signal: str | None = None,
    limit: int = TELEMETRY_EXPORT_LIMIT,
) -> tuple[list, bool]:
    """Fetch aggregated (1-minute) telemetry from continuous aggregate."""
    conditions = ["capture_session_id = ANY($1::uuid[])"]
    params: list = [capture_session_ids]
    idx = 2

    if sensor_id is not None:
        conditions.append(f"sensor_id = ${idx}")
        params.append(sensor_id)
        idx += 1

    if signal is not None:
        conditions.append(f"signal = ${idx}")
        params.append(signal)
        idx += 1

    where = " AND ".join(conditions)
    query = f"""
        SELECT
            bucket, sensor_id, signal, capture_session_id,
            sample_count,
            avg_raw, min_raw, max_raw,
            avg_physical, min_physical, max_physical
        FROM telemetry_1m
        WHERE {where}
        ORDER BY bucket ASC
        LIMIT {limit + 1}
    """
    async with pool.acquire() as conn:
        rows = await conn.fetch(query, *params)

    truncated = len(rows) > limit
    if truncated:
        rows = rows[:limit]
    return rows, truncated


# ---------------------------------------------------------------------------
# Formatters
# ---------------------------------------------------------------------------

def _val(v) -> str:
    return str(v) if v is not None else ""


def _fval(v):
    return float(v) if v is not None else None


def _telemetry_rows_to_csv(rows, value_mode: str) -> str:
    buf = io.StringIO()
    writer = csv.writer(buf)
    header = ["timestamp", "sensor_id", "signal"]
    if value_mode in ("raw", "both"):
        header.append("raw_value")
    if value_mode in ("physical", "both"):
        header.append("physical_value")
    header.extend(["conversion_status", "capture_session_id"])
    writer.writerow(header)

    for row in rows:
        line = [
            row["timestamp"].isoformat() if row["timestamp"] else "",
            str(row["sensor_id"]),
            row["signal"] or "",
        ]
        if value_mode in ("raw", "both"):
            line.append(_val(row["raw_value"]))
        if value_mode in ("physical", "both"):
            line.append(_val(row["physical_value"]))
        line.extend([
            row["conversion_status"] or "",
            str(row["capture_session_id"]) if row["capture_session_id"] else "",
        ])
        writer.writerow(line)
    return buf.getvalue()


def _telemetry_rows_to_json(rows, value_mode: str) -> str:
    result = []
    for row in rows:
        item: dict = {
            "timestamp": row["timestamp"].isoformat() if row["timestamp"] else None,
            "sensor_id": str(row["sensor_id"]),
            "signal": row["signal"],
        }
        if value_mode in ("raw", "both"):
            item["raw_value"] = _fval(row["raw_value"])
        if value_mode in ("physical", "both"):
            item["physical_value"] = _fval(row["physical_value"])
        item["conversion_status"] = row["conversion_status"]
        item["capture_session_id"] = (
            str(row["capture_session_id"]) if row["capture_session_id"] else None
        )
        result.append(item)
    return json.dumps(result, ensure_ascii=False, indent=2)


def _aggregated_to_csv(rows) -> str:
    buf = io.StringIO()
    writer = csv.writer(buf)
    writer.writerow([
        "bucket", "sensor_id", "signal", "capture_session_id", "sample_count",
        "avg_raw", "min_raw", "max_raw",
        "avg_physical", "min_physical", "max_physical",
    ])
    for row in rows:
        writer.writerow([
            row["bucket"].isoformat() if row["bucket"] else "",
            str(row["sensor_id"]),
            row["signal"] or "",
            str(row["capture_session_id"]) if row["capture_session_id"] else "",
            row["sample_count"] or 0,
            _val(row["avg_raw"]), _val(row["min_raw"]), _val(row["max_raw"]),
            _val(row["avg_physical"]), _val(row["min_physical"]), _val(row["max_physical"]),
        ])
    return buf.getvalue()


def _aggregated_to_json(rows) -> str:
    result = []
    for row in rows:
        result.append({
            "bucket": row["bucket"].isoformat() if row["bucket"] else None,
            "sensor_id": str(row["sensor_id"]),
            "signal": row["signal"],
            "capture_session_id": (
                str(row["capture_session_id"]) if row["capture_session_id"] else None
            ),
            "sample_count": row["sample_count"] or 0,
            "avg_raw": _fval(row["avg_raw"]),
            "min_raw": _fval(row["min_raw"]),
            "max_raw": _fval(row["max_raw"]),
            "avg_physical": _fval(row["avg_physical"]),
            "min_physical": _fval(row["min_physical"]),
            "max_physical": _fval(row["max_physical"]),
        })
    return json.dumps(result, ensure_ascii=False, indent=2)


def _export_response(
    body: str, fmt: str, filename: str, truncated: bool,
) -> web.Response:
    content_type = "application/json" if fmt == "json" else "text/csv"
    headers: dict[str, str] = {
        "Content-Disposition": f'attachment; filename="{filename}"',
    }
    if truncated:
        headers["X-Export-Truncated"] = "true"
    return web.Response(text=body, content_type=content_type, headers=headers)


# ---------------------------------------------------------------------------
# Route handlers
# ---------------------------------------------------------------------------


@routes.get(
    "/api/v1/runs/{run_id}/capture-sessions/{session_id}/telemetry/export"
)
async def export_session_telemetry(request: web.Request):
    """Export telemetry readings for a single capture session.

    Query params:
      - project_id (required or from header)
      - format: csv | json (default csv)
      - sensor_id: filter by sensor (optional)
      - signal: filter by signal name (optional)
      - include_late: true | false (default true)
      - raw_or_physical: raw | physical | both (default both)
      - aggregation: 1m (optional; uses continuous aggregate)
    """
    user = await require_current_user(request)
    project_id = resolve_project_id(
        user, request.rel_url.query.get("project_id"),
    )
    ensure_project_access(user, project_id)

    run_id = parse_uuid(request.match_info["run_id"], "run_id")
    session_id = parse_uuid(request.match_info["session_id"], "session_id")

    run_service = await get_run_service(request)
    await run_service.get_run(project_id, run_id)
    cs_service = await get_capture_session_service(request)
    await cs_service.get_session(project_id, session_id)

    fmt = request.rel_url.query.get("format", "csv").lower()
    if fmt not in ("csv", "json"):
        raise web.HTTPBadRequest(text="format must be csv or json")

    value_mode = _parse_value_mode(request.rel_url.query.get("raw_or_physical"))
    include_late = (
        request.rel_url.query.get("include_late", "true").lower() != "false"
    )
    aggregation = request.rel_url.query.get("aggregation", "").lower()
    sensor_id_raw = request.rel_url.query.get("sensor_id")
    sensor_id = parse_uuid(sensor_id_raw, "sensor_id") if sensor_id_raw else None
    signal_filter = request.rel_url.query.get("signal") or None

    pool = await get_pool()

    if aggregation == "1m":
        rows, truncated = await _fetch_telemetry_aggregated(
            pool, [session_id],
            sensor_id=sensor_id, signal=signal_filter,
        )
        body = (
            _aggregated_to_json(rows) if fmt == "json"
            else _aggregated_to_csv(rows)
        )
        filename = f"telemetry_agg_{session_id}.{fmt}"
    else:
        rows, truncated = await _fetch_telemetry_rows(
            pool, [session_id],
            sensor_id=sensor_id, signal=signal_filter,
            include_late=include_late,
        )
        body = (
            _telemetry_rows_to_json(rows, value_mode) if fmt == "json"
            else _telemetry_rows_to_csv(rows, value_mode)
        )
        filename = f"telemetry_{session_id}.{fmt}"

    return _export_response(body, fmt, filename, truncated)


@routes.get("/api/v1/runs/{run_id}/telemetry/export")
async def export_run_telemetry(request: web.Request):
    """Export telemetry readings for all capture sessions of a run.

    Query params:
      - project_id (required or from header)
      - format: csv | json (default csv)
      - capture_session_id: filter to specific session (optional)
      - sensor_id: filter by sensor (optional)
      - signal: filter by signal name (optional)
      - include_late: true | false (default true)
      - raw_or_physical: raw | physical | both (default both)
      - aggregation: 1m (optional; uses continuous aggregate)
    """
    user = await require_current_user(request)
    project_id = resolve_project_id(
        user, request.rel_url.query.get("project_id"),
    )
    ensure_project_access(user, project_id)

    run_id = parse_uuid(request.match_info["run_id"], "run_id")
    run_service = await get_run_service(request)
    await run_service.get_run(project_id, run_id)

    fmt = request.rel_url.query.get("format", "csv").lower()
    if fmt not in ("csv", "json"):
        raise web.HTTPBadRequest(text="format must be csv or json")

    value_mode = _parse_value_mode(request.rel_url.query.get("raw_or_physical"))
    include_late = (
        request.rel_url.query.get("include_late", "true").lower() != "false"
    )
    aggregation = request.rel_url.query.get("aggregation", "").lower()
    sensor_id_raw = request.rel_url.query.get("sensor_id")
    sensor_id = parse_uuid(sensor_id_raw, "sensor_id") if sensor_id_raw else None
    signal_filter = request.rel_url.query.get("signal") or None

    # Resolve capture session IDs
    cs_filter_raw = request.rel_url.query.get("capture_session_id")
    if cs_filter_raw:
        session_ids = [parse_uuid(cs_filter_raw, "capture_session_id")]
    else:
        cs_service = await get_capture_session_service(request)
        sessions, _total = await cs_service.list_sessions_for_run(
            project_id, run_id, limit=500, offset=0,
        )
        session_ids = [s.id for s in sessions]

    if not session_ids:
        body = "[]" if fmt == "json" else ""
        return _export_response(
            body, fmt, f"telemetry_run_{run_id}.{fmt}", False,
        )

    pool = await get_pool()

    if aggregation == "1m":
        rows, truncated = await _fetch_telemetry_aggregated(
            pool, session_ids,
            sensor_id=sensor_id, signal=signal_filter,
        )
        body = (
            _aggregated_to_json(rows) if fmt == "json"
            else _aggregated_to_csv(rows)
        )
        filename = f"telemetry_run_{run_id}_agg.{fmt}"
    else:
        rows, truncated = await _fetch_telemetry_rows(
            pool, session_ids,
            sensor_id=sensor_id, signal=signal_filter,
            include_late=include_late,
        )
        body = (
            _telemetry_rows_to_json(rows, value_mode) if fmt == "json"
            else _telemetry_rows_to_csv(rows, value_mode)
        )
        filename = f"telemetry_run_{run_id}.{fmt}"

    return _export_response(body, fmt, filename, truncated)
