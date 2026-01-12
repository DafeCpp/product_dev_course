"""API utilities."""
from __future__ import annotations

from typing import Any

from aiohttp import web


async def read_json(request: web.Request) -> dict[str, Any]:
    try:
        data = await request.json()
    except Exception as exc:  # pragma: no cover
        raise web.HTTPBadRequest(text="Invalid JSON payload") from exc
    if not isinstance(data, dict):
        raise web.HTTPBadRequest(text="JSON body must be an object")
    return data

