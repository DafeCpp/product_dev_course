"""Background webhook dispatcher (polls outbox and delivers HTTP POST)."""
from __future__ import annotations

import asyncio
import hmac
import json
from datetime import datetime, timedelta, timezone
from hashlib import sha256
from typing import Any

from aiohttp import ClientSession, ClientTimeout, web

from backend_common.db.pool import get_pool_service as get_pool

from experiment_service.domain.webhooks import WebhookDelivery
from experiment_service.repositories.webhooks import WebhookDeliveryRepository
from experiment_service.settings import settings

_WEBHOOK_SESSION_KEY = "webhook_http_session"
_WEBHOOK_TASK_KEY = "webhook_dispatcher_task"


def _signature(secret: str, body_bytes: bytes) -> str:
    digest = hmac.new(secret.encode("utf-8"), body_bytes, sha256).hexdigest()
    return f"sha256={digest}"


def _backoff_seconds(attempt: int) -> int:
    # attempt is 1-based
    # Use bitshift for stable int typing under mypy.
    exp: int = min(attempt, 6)
    delay: int = 1 << exp
    return min(60, delay)


async def _deliver(
    session: ClientSession,
    delivery: WebhookDelivery,
    *,
    timeout_s: float,
) -> tuple[bool, str | None]:
    body_bytes = json.dumps(delivery.request_body, separators=(",", ":"), ensure_ascii=False).encode(
        "utf-8"
    )
    headers = {
        "Content-Type": "application/json",
        "X-Webhook-Event": delivery.event_type,
        "X-Webhook-Delivery-Id": str(delivery.id),
    }
    if delivery.secret:
        headers["X-Webhook-Signature"] = _signature(delivery.secret, body_bytes)
    try:
        timeout = ClientTimeout(total=timeout_s)
        async with session.post(
            delivery.target_url,
            data=body_bytes,
            headers=headers,
            timeout=timeout,
        ) as resp:
            if 200 <= resp.status < 300:
                return True, None
            text = await resp.text()
            return False, f"HTTP {resp.status}: {text[:2000]}"
    except Exception as exc:  # pragma: no cover (network errors depend on env)
        return False, str(exc)


async def _dispatcher_loop(app: web.Application) -> None:
    pool = await get_pool()
    repo = WebhookDeliveryRepository(pool)
    session: ClientSession = app[_WEBHOOK_SESSION_KEY]

    while True:
        due = await repo.list_due_pending(limit=100)
        if not due:
            await asyncio.sleep(settings.webhook_dispatch_interval_seconds)
            continue

        for delivery in due:
            new_attempt = delivery.attempt_count + 1
            ok, err = await _deliver(
                session, delivery, timeout_s=settings.webhook_request_timeout_seconds
            )
            if ok:
                await repo.mark_attempt(
                    delivery.id,
                    success=True,
                    status="succeeded",
                    last_error=None,
                    next_attempt_at=None,
                    attempt_count=new_attempt,
                )
                continue

            if new_attempt >= settings.webhook_max_attempts:
                await repo.mark_attempt(
                    delivery.id,
                    success=False,
                    status="failed",
                    last_error=err,
                    next_attempt_at=None,
                    attempt_count=new_attempt,
                )
                continue

            next_at = datetime.now(timezone.utc) + timedelta(seconds=_backoff_seconds(new_attempt))
            await repo.mark_attempt(
                delivery.id,
                success=False,
                status="pending",
                last_error=err,
                next_attempt_at=next_at,
                attempt_count=new_attempt,
            )


async def start_webhook_dispatcher(app: web.Application) -> None:
    timeout = ClientTimeout(total=settings.webhook_request_timeout_seconds)
    session = ClientSession(timeout=timeout)
    app[_WEBHOOK_SESSION_KEY] = session
    app[_WEBHOOK_TASK_KEY] = asyncio.create_task(_dispatcher_loop(app))


async def stop_webhook_dispatcher(app: web.Application) -> None:
    task = app.get(_WEBHOOK_TASK_KEY)
    if task is not None:
        task.cancel()
        try:
            await task
        except asyncio.CancelledError:
            pass
    session = app.get(_WEBHOOK_SESSION_KEY)
    if session is not None:
        await session.close()

