"""Helper utilities for API handlers."""
# pyright: reportMissingImports=false
from __future__ import annotations

from typing import Any
from uuid import UUID

from aiohttp import web

# Re-export read_json from backend_common so existing imports keep working.
from backend_common.aiohttp_app import read_json as read_json  # noqa: F401


def parse_uuid(value: str, label: str) -> UUID:
    try:
        uuid_str = value if isinstance(value, str) else str(value)
        return UUID(uuid_str)
    except (ValueError, TypeError) as exc:
        raise web.HTTPBadRequest(text=f"Invalid {label}") from exc


def pagination_params(
    request: web.Request,
    *,
    default_limit: int = 50,
    max_limit: int = 100,
) -> tuple[int, int]:
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


def paginated_response(
    items: list[Any],
    *,
    limit: int,
    offset: int,
    key: str,
    total: int,
) -> dict[str, Any]:
    page = offset // limit + 1 if limit else 1
    return {
        key: items,
        "total": total,
        "page": page,
        "page_size": limit,
    }

