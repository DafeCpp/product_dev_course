"""API utilities."""
from __future__ import annotations

# Re-export read_json from backend_common so existing imports keep working.
from backend_common.aiohttp_app import read_json as read_json  # noqa: F401

