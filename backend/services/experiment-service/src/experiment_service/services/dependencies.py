"""Shared aiohttp dependency placeholders."""
from __future__ import annotations

from dataclasses import dataclass
from uuid import UUID

from aiohttp import web


@dataclass
class UserContext:
    user_id: UUID
    project_roles: dict[UUID, str]


async def require_current_user(request: web.Request) -> UserContext:
    """Placeholder hook. Integrate with Auth Service in future iterations."""
    raise web.HTTPUnauthorized(reason="Auth integration not implemented")

