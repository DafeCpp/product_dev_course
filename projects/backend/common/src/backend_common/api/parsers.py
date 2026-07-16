"""Request parameter parsing helpers.

Shared utilities for parsing and validating common parameter types
(UUIDs, datetimes, pagination) from aiohttp request objects.
"""
from __future__ import annotations

from datetime import datetime, timezone
from uuid import UUID

from aiohttp import web


def parse_uuid(value: str, label: str) -> UUID:
    """Parse a string as UUID, raising HTTPBadRequest on invalid input."""
    try:
        return UUID(value if isinstance(value, str) else str(value))
    except (ValueError, TypeError) as exc:
        raise web.HTTPBadRequest(text=f"Invalid {label}") from exc


def parse_optional_uuid(value: str | None, label: str = "UUID") -> UUID | None:
    """Parse an optional UUID string. Returns None if value is falsy."""
    if not value:
        return None
    return parse_uuid(value, label)


def parse_datetime(value: str | None, label: str) -> datetime | None:
    """Parse an ISO-8601 datetime string. Returns None if value is empty."""
    if not value:
        return None
    try:
        dt = datetime.fromisoformat(value)
        if dt.tzinfo is None:
            dt = dt.replace(tzinfo=timezone.utc)
        return dt
    except (ValueError, TypeError) as exc:
        raise web.HTTPBadRequest(text=f"Invalid datetime for {label}: {value}") from exc


def parse_int(value: str | None, *, default: int) -> int:
    """Parse an integer query parameter, returning ``default`` when omitted."""
    if value is None:
        return default
    try:
        return int(value)
    except ValueError as exc:
        raise web.HTTPBadRequest(text="Invalid integer query param") from exc


def parse_bool(value: str | None, *, default: bool) -> bool:
    """Parse common boolean query representations."""
    if value is None:
        return default
    normalized = value.strip().lower()
    if normalized in ("1", "true", "yes", "on"):
        return True
    if normalized in ("0", "false", "no", "off"):
        return False
    raise web.HTTPBadRequest(text="Invalid boolean query param")


def parse_rfc3339(value: str | None, *, default: datetime | None = None) -> datetime | None:
    """Parse an RFC3339/ISO-8601 value and normalize it to UTC."""
    if value is None or not value.strip():
        return default
    raw = value.strip()
    if raw.endswith("Z"):
        raw = raw[:-1] + "+00:00"
    try:
        result = datetime.fromisoformat(raw)
    except ValueError as exc:
        raise web.HTTPBadRequest(text="Invalid since_ts (expected ISO8601/RFC3339)") from exc
    if result.tzinfo is None:
        result = result.replace(tzinfo=timezone.utc)
    return result.astimezone(timezone.utc)


def pagination_params(
    request: web.Request,
    *,
    default_limit: int = 50,
    max_limit: int = 100,
) -> tuple[int, int]:
    """Extract and validate limit/offset pagination parameters."""
    query = request.rel_url.query
    try:
        limit = int(query.get("limit", str(default_limit)))
        offset = int(query.get("offset", "0"))
    except ValueError as exc:
        raise web.HTTPBadRequest(text="limit and offset must be integers") from exc
    if limit <= 0:
        limit = default_limit
    limit = min(limit, max_limit)
    if offset < 0:
        offset = 0
    return limit, offset
