"""HTTP client for sending audit entries to auth-service."""
from __future__ import annotations

import asyncio
from typing import Any
from uuid import UUID

import structlog
from aiohttp import ClientSession, ClientTimeout

logger = structlog.get_logger(__name__)

_AUDIT_SESSION_KEY = "audit_client_session"
_AUDIT_CLIENT_KEY = "audit_client"


class AuditClient:
    """Fire-and-forget audit client that sends entries to auth-service."""

    def __init__(self, auth_service_url: str, session: ClientSession) -> None:
        # Strip trailing slash; auth_service_url already includes /api/v1
        base = str(auth_service_url).rstrip("/")
        self._endpoint = f"{base}/internal/audit"
        self._session = session

    def log_action(
        self,
        actor_id: UUID,
        action: str,
        scope_type: str,
        *,
        scope_id: UUID | None = None,
        target_type: str | None = None,
        target_id: str | None = None,
        details: dict[str, Any] | None = None,
        ip_address: str | None = None,
        user_agent: str | None = None,
    ) -> None:
        """Schedule an audit entry write (non-blocking)."""
        payload: dict[str, Any] = {
            "actor_id": str(actor_id),
            "action": action,
            "scope_type": scope_type,
        }
        if scope_id is not None:
            payload["scope_id"] = str(scope_id)
        if target_type is not None:
            payload["target_type"] = target_type
        if target_id is not None:
            payload["target_id"] = target_id
        if details is not None:
            payload["details"] = details
        if ip_address is not None:
            payload["ip_address"] = ip_address
        if user_agent is not None:
            payload["user_agent"] = user_agent

        asyncio.create_task(self._send(payload))

    async def _send(self, payload: dict[str, Any]) -> None:
        try:
            async with self._session.post(self._endpoint, json=payload) as resp:
                if resp.status >= 400:
                    text = await resp.text()
                    logger.warning(
                        "Audit write failed",
                        status=resp.status,
                        action=payload.get("action"),
                        response=text[:200],
                    )
        except Exception as e:
            logger.warning("Audit HTTP error", action=payload.get("action"), error=str(e))
