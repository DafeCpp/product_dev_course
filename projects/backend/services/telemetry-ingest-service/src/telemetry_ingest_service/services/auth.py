"""User-token authorization via auth-service."""
from __future__ import annotations

from uuid import UUID

import aiohttp
import structlog

from telemetry_ingest_service.core.exceptions import AuthServiceError, NotFoundError, UnauthorizedError
from backend_common.core.exceptions import ForbiddenError
from telemetry_ingest_service.settings import settings

logger = structlog.get_logger(__name__)


def normalize_bearer(value: str | None) -> str | None:
    """Strip an optional Bearer prefix from an HTTP token value."""
    if not value:
        return None
    normalized = value.strip()
    if normalized.lower().startswith("bearer "):
        normalized = normalized[7:].strip()
    return normalized or None


class UserTokenAuthorizer:
    async def authorize(self, *, token: str, project_id: UUID) -> None:
        base = settings.auth_service_url.rstrip("/")
        if base.endswith("/api/v1"):
            base = base[:-len("/api/v1")]
        headers = {"Authorization": f"Bearer {token}"}
        try:
            async with aiohttp.ClientSession() as session:
                async with session.get(f"{base}/auth/me", headers=headers) as response:
                    if response.status != 200:
                        raise UnauthorizedError("Unauthorized")
                    user_id = (await response.json()).get("id")
                    if not user_id:
                        raise UnauthorizedError("Unauthorized")
                async with session.get(f"{base}/projects/{project_id}/members", headers=headers) as response:
                    if response.status == 403:
                        raise ForbiddenError("Forbidden")
                    if response.status == 404:
                        raise NotFoundError("Project not found")
                    if response.status != 200:
                        raise AuthServiceError("Auth service error")
                    members = (await response.json()).get("members") or []
                    if not any(str(member.get("user_id")) == str(user_id) for member in members):
                        raise ForbiddenError("Forbidden")
        except (UnauthorizedError, ForbiddenError, NotFoundError, AuthServiceError):
            raise
        except Exception as exc:
            logger.warning("auth_service_error", error=str(exc))
            raise AuthServiceError("Auth service error") from exc
