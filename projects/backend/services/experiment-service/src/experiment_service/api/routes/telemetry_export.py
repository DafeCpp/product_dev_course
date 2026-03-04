"""Telemetry data export endpoints (CSV / JSON) — streaming via aiohttp StreamResponse."""
from __future__ import annotations

import csv
import io
import json
from uuid import UUID

from aiohttp import web

from backend_common.db.pool import get_pool_service as get_pool
from experiment_service.api.utils import parse_uuid
from experiment_service.core.exceptions import NotFoundError
from experiment_service.services.dependencies import (
    ensure_project_access,
    get_capture_session_service,
    get_run_service,
    require_current_user,
    resolve_project_id,
)

routes = web.RouteTableDef()

STREAM_BATCH_SIZE = 5_000


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------


def _parse_value_mode(raw: str | None) -> str:
    mode = (raw or "both").lower()
    if mode not in ("raw", "physical", "both"):
        raise web.HTTPBadRequest(text="raw_or_physical must be raw, physical, or both")
    return mode


def _val(v) -> str:
    return str(v) if v is not None else ""


def _fval(v):
    return float(v) if v is not None else None


def _build_raw_query(
    capture_session_ids: list[UUID],
    *,
    sensor_id: UUID | None = None,
    signal: str | None = None,
    include_late: bool = True,
) -> tuple[str, list]:
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
    """
    return query, params


def _build_agg_query(
    capture_session_ids: list[UUID],
    *,
    sensor_id: UUID | None = None,
    signal: str | None = None,
) -> tuple[str, list]:
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
    """
    return query, params


# ---------------------------------------------------------------------------
# Row formatters (used inside streaming helpers)
# ---------------------------------------------------------------------------


def _write_raw_csv_row(writer, row, value_mode: str) -> None:
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


def _raw_row_to_dict(row, value_mode: str) -> dict:
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
    return item


def _write_agg_csv_row(writer, row) -> None:
    writer.writerow([
        row["bucket"].isoformat() if row["bucket"] else "",
        str(row["sensor_id"]),
        row["signal"] or "",
        str(row["capture_session_id"]) if row["capture_session_id"] else "",
        row["sample_count"] or 0,
        _val(row["avg_raw"]), _val(row["min_raw"]), _val(row["max_raw"]),
        _val(row["avg_physical"]), _val(row["min_physical"]), _val(row["max_physical"]),
    ])


def _agg_row_to_dict(row) -> dict:
    return {
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
    }


# ---------------------------------------------------------------------------
# Streaming helpers
# ---------------------------------------------------------------------------


async def _stream_csv(
    request: web.Request,
    pool,
    query: str,
    params: list,
    filename: str,
    header: list[str],
    row_fn,  # callable(writer, row) -> None
) -> web.StreamResponse:
    response = web.StreamResponse()
    response.content_type = "text/csv"
    response.headers["Content-Disposition"] = f'attachment; filename="{filename}"'
    await response.prepare(request)

    buf = io.StringIO()
    writer = csv.writer(buf)
    writer.writerow(header)
    await response.write(buf.getvalue().encode())

    async with pool.acquire() as conn:
        async with conn.transaction():
            batch: list = []
            async for row in conn.cursor(query, *params, prefetch=STREAM_BATCH_SIZE):
                batch.append(row)
                if len(batch) >= STREAM_BATCH_SIZE:
                    buf = io.StringIO()
                    writer = csv.writer(buf)
                    for r in batch:
                        row_fn(writer, r)
                    await response.write(buf.getvalue().encode())
                    batch = []
            if batch:
                buf = io.StringIO()
                writer = csv.writer(buf)
                for r in batch:
                    row_fn(writer, r)
                await response.write(buf.getvalue().encode())

    await response.write_eof()
    return response


async def _stream_json(
    request: web.Request,
    pool,
    query: str,
    params: list,
    filename: str,
    row_fn,  # callable(row) -> dict
) -> web.StreamResponse:
    response = web.StreamResponse()
    response.content_type = "application/json"
    response.headers["Content-Disposition"] = f'attachment; filename="{filename}"'
    await response.prepare(request)

    await response.write(b"[")
    first = True

    async with pool.acquire() as conn:
        async with conn.transaction():
            async for row in conn.cursor(query, *params, prefetch=STREAM_BATCH_SIZE):
                chunk = ("\n  " if first else ",\n  ") + json.dumps(
                    row_fn(row), ensure_ascii=False
                )
                await response.write(chunk.encode())
                first = False

    await response.write(b"]" if first else b"\n]")
    await response.write_eof()
    return response


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
    try:
        await run_service.get_run(project_id, run_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc
    cs_service = await get_capture_session_service(request)
    try:
        await cs_service.get_session(project_id, session_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc

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
        query, params = _build_agg_query(
            [session_id], sensor_id=sensor_id, signal=signal_filter,
        )
        filename = f"telemetry_agg_{session_id}.{fmt}"
        if fmt == "json":
            return await _stream_json(request, pool, query, params, filename, _agg_row_to_dict)
        header = [
            "bucket", "sensor_id", "signal", "capture_session_id", "sample_count",
            "avg_raw", "min_raw", "max_raw",
            "avg_physical", "min_physical", "max_physical",
        ]
        return await _stream_csv(request, pool, query, params, filename, header, _write_agg_csv_row)

    query, params = _build_raw_query(
        [session_id], sensor_id=sensor_id, signal=signal_filter, include_late=include_late,
    )
    filename = f"telemetry_{session_id}.{fmt}"
    if fmt == "json":
        return await _stream_json(
            request, pool, query, params, filename,
            lambda row: _raw_row_to_dict(row, value_mode),
        )
    csv_header = ["timestamp", "sensor_id", "signal"]
    if value_mode in ("raw", "both"):
        csv_header.append("raw_value")
    if value_mode in ("physical", "both"):
        csv_header.append("physical_value")
    csv_header.extend(["conversion_status", "capture_session_id"])
    return await _stream_csv(
        request, pool, query, params, filename, csv_header,
        lambda writer, row: _write_raw_csv_row(writer, row, value_mode),
    )


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
    try:
        await run_service.get_run(project_id, run_id)
    except NotFoundError as exc:
        raise web.HTTPNotFound(text=str(exc)) from exc

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
        content_type = "application/json" if fmt == "json" else "text/csv"
        return web.Response(
            text="[]" if fmt == "json" else "",
            content_type=content_type,
            headers={
                "Content-Disposition": f'attachment; filename="telemetry_run_{run_id}.{fmt}"',
            },
        )

    pool = await get_pool()

    if aggregation == "1m":
        query, params = _build_agg_query(
            session_ids, sensor_id=sensor_id, signal=signal_filter,
        )
        filename = f"telemetry_run_{run_id}_agg.{fmt}"
        if fmt == "json":
            return await _stream_json(request, pool, query, params, filename, _agg_row_to_dict)
        header = [
            "bucket", "sensor_id", "signal", "capture_session_id", "sample_count",
            "avg_raw", "min_raw", "max_raw",
            "avg_physical", "min_physical", "max_physical",
        ]
        return await _stream_csv(request, pool, query, params, filename, header, _write_agg_csv_row)

    query, params = _build_raw_query(
        session_ids, sensor_id=sensor_id, signal=signal_filter, include_late=include_late,
    )
    filename = f"telemetry_run_{run_id}.{fmt}"
    if fmt == "json":
        return await _stream_json(
            request, pool, query, params, filename,
            lambda row: _raw_row_to_dict(row, value_mode),
        )
    csv_header = ["timestamp", "sensor_id", "signal"]
    if value_mode in ("raw", "both"):
        csv_header.append("raw_value")
    if value_mode in ("physical", "both"):
        csv_header.append("physical_value")
    csv_header.extend(["conversion_status", "capture_session_id"])
    return await _stream_csv(
        request, pool, query, params, filename, csv_header,
        lambda writer, row: _write_raw_csv_row(writer, row, value_mode),
    )
