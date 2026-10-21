"""Contract for the ``AUTH_SERVICE_URL`` setting shared by backend services.

``AUTH_SERVICE_URL`` is the auth-service root (``http://auth-service:8001``),
without an API prefix: each client appends the full path it calls
(``/auth/me``, ``/api/v1/internal/audit``). A legacy value ending in
``/api/v1`` is still accepted and reduced to the root, so a deployment that
sets either form reaches the same routes.
"""
from __future__ import annotations

_API_PREFIX = "/api/v1"


def auth_service_root(url: str) -> str:
    """Return the auth-service root URL with no trailing slash or ``/api/v1``."""
    base = str(url).rstrip("/")
    if base.endswith(_API_PREFIX):
        base = base[: -len(_API_PREFIX)].rstrip("/")
    return base
