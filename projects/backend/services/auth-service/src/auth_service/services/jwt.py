"""JWT token utilities."""
from __future__ import annotations

import time
import uuid
from typing import Any

import jwt  # type: ignore[import-untyped]

from auth_service.settings import settings


def create_access_token(
    user_id: str,
    is_superadmin: bool = False,
    system_permissions: list[str] | None = None,
    password_change_required: bool = False,
) -> str:
    """Create access token with RBAC v2 claims.

    Args:
        user_id: User identifier
        is_superadmin: True if user has superadmin role (short-circuit for all permissions)
        system_permissions: List of system permission IDs granted to the user
        password_change_required: When True, embeds ``pcr: true`` claim so
            downstream services can enforce the password-change wall.
    """
    now = int(time.time())
    payload: dict[str, Any] = {
        "sub": user_id,
        "type": "access",
        "iat": now,
        "exp": now + settings.access_token_ttl_sec,
    }

    # Add RBAC v2 claims
    if is_superadmin:
        payload["sa"] = True
    else:
        # Only include system permissions if not superadmin
        # (superadmin has all permissions implicitly)
        if system_permissions:
            payload["sys"] = system_permissions

    # Password-change-required claim — omitted when False to keep token compact
    if password_change_required:
        payload["pcr"] = True

    return jwt.encode(payload, settings.jwt_secret, algorithm=settings.jwt_algorithm)


def create_refresh_token(user_id: str, family_id: str | None = None) -> str:
    """Create refresh token.

    Args:
        user_id: User identifier.
        family_id: Token family UUID string. When provided the claim ``fid`` is
            embedded in the payload to enable reuse-detection.
    """
    now = int(time.time())
    payload: dict[str, Any] = {
        "sub": user_id,
        "type": "refresh",
        "jti": str(uuid.uuid4()),
        "iat": now,
        "exp": now + settings.refresh_token_ttl_sec,
    }
    if family_id is not None:
        payload["fid"] = family_id
    return jwt.encode(payload, settings.jwt_secret, algorithm=settings.jwt_algorithm)


def decode_token(token: str) -> dict[str, Any]:
    """Decode and validate JWT token."""
    try:
        payload = jwt.decode(
            token,
            settings.jwt_secret,
            algorithms=[settings.jwt_algorithm],
        )
        return dict(payload)  # type: ignore[return-value]
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


def get_jti_from_token(token: str) -> str:
    """Extract jti claim from token."""
    payload = decode_token(token)
    jti = payload.get("jti")
    if not jti:
        raise ValueError("Token missing jti")
    return str(jti)


def get_claims_from_token(token: str) -> dict[str, Any]:
    """Extract all claims from token including RBAC v2 claims.

    Returns:
        dict with keys:
            - sub: user ID
            - sa: bool (True if superadmin)
            - sys: list[str] (system permissions, empty if superadmin)
            - pcr: bool (True if password change is required)
    """
    payload = decode_token(token)
    return {
        "sub": payload.get("sub"),
        "sa": payload.get("sa", False),
        "sys": payload.get("sys", []),
        "pcr": payload.get("pcr", False),
    }


