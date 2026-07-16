"""Request metadata helpers shared between aiohttp services."""
from __future__ import annotations

from aiohttp import web


def extract_client_ip(request: web.Request) -> str | None:
    """Extract the client IP address, respecting ``X-Forwarded-For``."""
    forwarded_for = request.headers.get("X-Forwarded-For")
    if forwarded_for:
        return forwarded_for.split(",")[0].strip()
    return request.remote or None
