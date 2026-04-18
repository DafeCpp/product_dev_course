"""Helper utilities for API handlers."""
# pyright: reportMissingImports=false
from __future__ import annotations

from typing import Any

# Re-export shared utilities from backend_common so existing imports keep working.
from backend_common.aiohttp_app import read_json as read_json  # noqa: F401
from backend_common.api.parsers import parse_uuid as parse_uuid  # noqa: F401
from backend_common.api.parsers import parse_datetime as parse_datetime  # noqa: F401
from backend_common.api.parsers import pagination_params as pagination_params  # noqa: F401


def parse_tags_filter(value: str | None) -> list[str] | None:
    """Parse comma-separated tags filter. Returns None if empty."""
    if not value:
        return None
    tags = [t.strip() for t in value.split(",") if t.strip()]
    return tags if tags else None


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

