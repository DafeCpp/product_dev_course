"""Webhook domain service (subscriptions + emitting events)."""
from __future__ import annotations

from datetime import datetime, timezone
from typing import Any, List
from uuid import UUID

from experiment_service.domain.webhooks import WebhookDelivery, WebhookSubscription
from experiment_service.repositories.webhooks import (
    WebhookDeliveryRepository,
    WebhookSubscriptionRepository,
)


class WebhookService:
    def __init__(
        self,
        subscription_repository: WebhookSubscriptionRepository,
        delivery_repository: WebhookDeliveryRepository,
    ):
        self._subscriptions = subscription_repository
        self._deliveries = delivery_repository

    async def create_subscription(
        self,
        *,
        project_id: UUID,
        target_url: str,
        event_types: list[str],
        secret: str | None,
    ) -> WebhookSubscription:
        return await self._subscriptions.create(
            project_id=project_id,
            target_url=target_url,
            event_types=event_types,
            secret=secret,
        )

    async def list_subscriptions(
        self, project_id: UUID, *, limit: int = 50, offset: int = 0
    ) -> tuple[List[WebhookSubscription], int]:
        return await self._subscriptions.list_by_project(project_id, limit=limit, offset=offset)

    async def delete_subscription(self, project_id: UUID, subscription_id: UUID) -> None:
        await self._subscriptions.delete(project_id, subscription_id)

    async def list_deliveries(
        self,
        project_id: UUID,
        *,
        status: str | None = None,
        limit: int = 50,
        offset: int = 0,
    ):
        return await self._deliveries.list_by_project(
            project_id,
            status=status,
            limit=limit,
            offset=offset,
        )

    async def retry_delivery(self, project_id: UUID, delivery_id: UUID) -> None:
        await self._deliveries.retry(project_id, delivery_id)

    async def emit(
        self,
        *,
        project_id: UUID,
        event_type: str,
        payload: dict[str, Any],
    ) -> List[WebhookDelivery]:
        subs = await self._subscriptions.list_active_matching(project_id, event_type)
        deliveries: List[WebhookDelivery] = []
        occurred_at = datetime.now(timezone.utc).isoformat()
        for sub in subs:
            body = {
                "event_type": event_type,
                "project_id": str(project_id),
                "occurred_at": occurred_at,
                "payload": payload,
            }
            delivery = await self._deliveries.enqueue(
                subscription_id=sub.id,
                project_id=project_id,
                event_type=event_type,
                target_url=sub.target_url,
                secret=sub.secret,
                request_body=body,
            )
            deliveries.append(delivery)
        return deliveries

