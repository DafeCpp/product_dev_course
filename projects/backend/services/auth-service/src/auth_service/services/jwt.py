"""JWT token utilities."""
from __future__ import annotations

import time
from typing import Any

import jwt  # type: ignore[import-untyped]

from auth_service.settings import settings


def create_access_token(user_id: str) -> str:
    """Create access token."""
    now = int(time.time())
    payload: dict[str, Any] = {
        "sub": user_id,
        "type": "access",
        "iat": now,
        "exp": now + settings.access_token_ttl_sec,
    }
    return jwt.encode(payload, settings.jwt_secret, algorithm=settings.jwt_algorithm)


def create_refresh_token(user_id: str) -> str:
    """Create refresh token."""
    now = int(time.time())
    payload: dict[str, Any] = {
        "sub": user_id,
        "type": "refresh",
        "iat": now,
        "exp": now + settings.refresh_token_ttl_sec,
    }
    return jwt.encode(payload, settings.jwt_secret, algorithm=settings.jwt_algorithm)


def decode_token(token: str) -> dict[str, Any]:
    """Decode and validate JWT token."""
    try:
        payload = jwt.decode(
            token,
            settings.jwt_secret,
            algorithms=[settings.jwt_algorithm],
        )
        return payload
    except jwt.ExpiredSignatureError:
        raise ValueError("Token expired")
    except jwt.InvalidTokenError as e:
        raise ValueError(f"Invalid token: {e}") from e


def get_user_id_from_token(token: str) -> str:
    """Extract user ID from token."""
    payload = decode_token(token)
    user_id = payload.get("sub")
    if not user_id:
        raise ValueError("Token missing user ID")
    return str(user_id)

