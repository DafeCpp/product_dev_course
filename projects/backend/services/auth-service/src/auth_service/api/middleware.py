"""aiohttp middlewares for auth-service."""
from __future__ import annotations

from typing import Awaitable, Callable

from aiohttp import web

from auth_service.api.utils import extract_bearer_token
from auth_service.services.jwt import decode_token

# Endpoints allowed when password change is required.
_PCR_ALLOWED_PATHS: frozenset[str] = frozenset(
    {
        "/auth/change-password",
        "/auth/logout",
        "/auth/me",
    }
)

Handler = Callable[[web.Request], Awaitable[web.StreamResponse]]
Middleware = Callable[[web.Request, Handler], Awaitable[web.StreamResponse]]


@web.middleware
async def password_change_required_middleware(
    request: web.Request,
    handler: Handler,
) -> web.StreamResponse:
    """Block requests when the JWT contains ``pcr: true``.

    Allowed paths are :data:`_PCR_ALLOWED_PATHS`. All other paths return
    403 with ``{"error": "password_change_required", ...}``.
    """
    token = extract_bearer_token(request)
    if token is None:
        return await handler(request)

    try:
        payload = decode_token(token)
    except ValueError:
        # Let the downstream handler deal with an invalid token.
        return await handler(request)

    if not payload.get("pcr", False):
        return await handler(request)

    # pcr=true — enforce the allowed-paths wall.
    path = request.path.rstrip("/") or "/"
    # Normalise so both "/auth/logout" and "/auth/logout/" match.
    if path in _PCR_ALLOWED_PATHS:
        return await handler(request)

    return web.json_response(
        {
            "error": "password_change_required",
            "message": "You must change your password before continuing",
        },
        status=403,
    )
