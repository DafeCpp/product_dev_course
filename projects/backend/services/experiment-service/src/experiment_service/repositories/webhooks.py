"""Webhook repositories (subscriptions + deliveries outbox)."""
from __future__ import annotations

import json
from datetime import datetime, timezone
from typing import Any, List, Tuple
from uuid import UUID

from asyncpg import Pool, Record  # type: ignore[import-untyped]

from experiment_service.core.exceptions import NotFoundError
from experiment_service.domain.webhooks import WebhookDelivery, WebhookSubscription
from experiment_service.repositories.base import BaseRepository


class WebhookSubscriptionRepository(BaseRepository):
    def __init__(self, pool: Pool):
        super().__init__(pool)

    @staticmethod
    def _to_model(record: Record) -> WebhookSubscription:
        payload = dict(record)
        return WebhookSubscription.model_validate(payload)

    async def create(
        self,
        *,
        project_id: UUID,
        target_url: str,
        event_types: list[str],
        secret: str | None,
    ) -> WebhookSubscription:
        record = await self._fetchrow(
            """
            INSERT INTO webhook_subscriptions (project_id, target_url, event_types, secret, is_active)
            VALUES ($1, $2, $3::text[], $4, true)
            RETURNING *
            """,
            project_id,
            target_url,
            event_types,
            secret,
        )
        assert record is not None
        return self._to_model(record)

    async def list_by_project(
        self, project_id: UUID, *, limit: int = 50, offset: int = 0
    ) -> Tuple[List[WebhookSubscription], int]:
        records = await self._fetch(
            """
            SELECT *,
                   COUNT(*) OVER() AS total_count
            FROM webhook_subscriptions
            WHERE project_id = $1
            ORDER BY created_at DESC
            LIMIT $2 OFFSET $3
            """,
            project_id,
            limit,
            offset,
        )
        items: List[WebhookSubscription] = []
        total: int | None = None
        for rec in records:
            rec_dict = dict(rec)
            total_value = rec_dict.pop("total_count", None)
            if total_value is not None:
                total = int(total_value)
            items.append(WebhookSubscription.model_validate(rec_dict))
        if total is None:
            total = await self._count_by_project(project_id)
        return items, total

    async def _count_by_project(self, project_id: UUID) -> int:
        record = await self._fetchrow(
            "SELECT COUNT(*) AS total FROM webhook_subscriptions WHERE project_id = $1",
            project_id,
        )
        return int(record["total"]) if record else 0

    async def delete(self, project_id: UUID, subscription_id: UUID) -> None:
        record = await self._fetchrow(
            """
            DELETE FROM webhook_subscriptions
            WHERE project_id = $1 AND id = $2
            RETURNING id
            """,
            project_id,
            subscription_id,
        )
        if record is None:
            raise NotFoundError("Webhook subscription not found")

    async def list_active_matching(
        self, project_id: UUID, event_type: str
    ) -> List[WebhookSubscription]:
        records = await self._fetch(
            """
            SELECT *
            FROM webhook_subscriptions
            WHERE project_id = $1
              AND is_active = true
              AND $2 = ANY(event_types)
            ORDER BY created_at ASC
            """,
            project_id,
            event_type,
        )
        return [WebhookSubscription.model_validate(dict(r)) for r in records]


class WebhookDeliveryRepository(BaseRepository):
    def __init__(self, pool: Pool):
        super().__init__(pool)

    @staticmethod
    def _to_model(record: Record) -> WebhookDelivery:
        return WebhookDelivery.model_validate(WebhookDeliveryRepository._normalize(dict(record)))

    @staticmethod
    def _normalize(payload: dict[str, Any]) -> dict[str, Any]:
        value = payload.get("request_body")
        if isinstance(value, str):
            payload["request_body"] = json.loads(value)
        return payload

    async def enqueue(
        self,
        *,
        subscription_id: UUID,
        project_id: UUID,
        event_type: str,
        target_url: str,
        secret: str | None,
        request_body: dict[str, Any],
        dedup_key: str | None = None,
    ) -> WebhookDelivery:
        body_json = json.dumps(request_body)
        if dedup_key:
            record = await self._fetchrow(
                """
                INSERT INTO webhook_deliveries (
                    subscription_id,
                    project_id,
                    event_type,
                    target_url,
                    secret,
                    request_body,
                    dedup_key,
                    status,
                    attempt_count,
                    next_attempt_at
                )
                VALUES ($1, $2, $3, $4, $5, $6::jsonb, $7, 'pending', 0, now())
                ON CONFLICT (dedup_key) DO NOTHING
                RETURNING *
                """,
                subscription_id,
                project_id,
                event_type,
                target_url,
                secret,
                body_json,
                dedup_key,
            )
            if record is None:
                record = await self._fetchrow(
                    "SELECT * FROM webhook_deliveries WHERE dedup_key = $1",
                    dedup_key,
                )
            assert record is not None
            return self._to_model(record)

        record = await self._fetchrow(
            """
            INSERT INTO webhook_deliveries (
                subscription_id,
                project_id,
                event_type,
                target_url,
                secret,
                request_body,
                status,
                attempt_count,
                next_attempt_at
            )
            VALUES ($1, $2, $3, $4, $5, $6::jsonb, 'pending', 0, now())
            RETURNING *
            """,
            subscription_id,
            project_id,
            event_type,
            target_url,
            secret,
            body_json,
        )
        assert record is not None
        return self._to_model(record)

    async def claim_due_pending(self, *, limit: int = 50) -> List[WebhookDelivery]:
        """
        Atomically claim due deliveries for processing.

        Uses row-level locking (FOR UPDATE SKIP LOCKED) so multiple dispatchers
        won't process the same delivery concurrently.

        Side-effects:
          - status -> in_progress
          - locked_at -> now()
          - attempt_count += 1
        """
        async with self._pool.acquire() as conn:
            async with conn.transaction():
                records = await conn.fetch(
                    """
                    WITH cte AS (
                        SELECT id
                        FROM webhook_deliveries
                        WHERE status = 'pending'
                          AND next_attempt_at <= now()
                        ORDER BY next_attempt_at ASC, created_at ASC
                        FOR UPDATE SKIP LOCKED
                        LIMIT $1
                    )
                    UPDATE webhook_deliveries d
                    SET status = 'in_progress',
                        locked_at = now(),
                        attempt_count = d.attempt_count + 1,
                        updated_at = now()
                    FROM cte
                    WHERE d.id = cte.id
                    RETURNING d.*
                    """,
                    limit,
                )
        return [self._to_model(r) for r in records]

    async def list_by_project(
        self,
        project_id: UUID,
        *,
        status: str | None = None,
        limit: int = 50,
        offset: int = 0,
    ) -> Tuple[List[WebhookDelivery], int]:
        where = ["project_id = $1"]
        values: list[Any] = [project_id]
        idx = 2
        if status is not None:
            where.append(f"status = ${idx}")
            values.append(status)
            idx += 1
        where_sql = " AND ".join(where)
        query = f"""
            SELECT *,
                   COUNT(*) OVER() AS total_count
            FROM webhook_deliveries
            WHERE {where_sql}
            ORDER BY created_at DESC
            LIMIT ${idx} OFFSET ${idx + 1}
        """
        values.extend([limit, offset])
        records = await self._fetch(query, *values)
        items: List[WebhookDelivery] = []
        total: int | None = None
        for rec in records:
            rec_dict = dict(rec)
            total_value = rec_dict.pop("total_count", None)
            if total_value is not None:
                total = int(total_value)
            items.append(WebhookDelivery.model_validate(self._normalize(rec_dict)))
        if total is None:
            total = await self._count_by_project(project_id, status=status)
        return items, total

    async def _count_by_project(self, project_id: UUID, *, status: str | None = None) -> int:
        if status is None:
            record = await self._fetchrow(
                "SELECT COUNT(*) AS total FROM webhook_deliveries WHERE project_id = $1",
                project_id,
            )
        else:
            record = await self._fetchrow(
                "SELECT COUNT(*) AS total FROM webhook_deliveries WHERE project_id = $1 AND status = $2",
                project_id,
                status,
            )
        return int(record["total"]) if record else 0

    async def retry(self, project_id: UUID, delivery_id: UUID) -> None:
        record = await self._fetchrow(
            """
            UPDATE webhook_deliveries
            SET status = 'pending',
                locked_at = NULL,
                next_attempt_at = now(),
                last_error = NULL,
                updated_at = now()
            WHERE project_id = $1 AND id = $2
            RETURNING id
            """,
            project_id,
            delivery_id,
        )
        if record is None:
            raise NotFoundError("Webhook delivery not found")

    async def mark_attempt(
        self,
        delivery_id: UUID,
        *,
        success: bool,
        status: str,
        last_error: str | None,
        next_attempt_at: datetime | None,
        attempt_count: int,
    ) -> None:
        # status is one of 'pending'/'in_progress'/'succeeded'/'dead_lettered' (plus legacy 'failed')
        await self._execute(
            """
            UPDATE webhook_deliveries
            SET status = $2,
                attempt_count = $3,
                last_error = $4,
                locked_at = NULL,
                next_attempt_at = COALESCE($5, next_attempt_at),
                updated_at = now()
            WHERE id = $1
            """,
            delivery_id,
            status,
            attempt_count,
            last_error,
            next_attempt_at,
        )

    async def reclaim_stuck(self, locked_before: datetime) -> int:
        """Release deliveries stuck in ``in_progress`` (e.g. after crash).

        Resets them to ``pending`` with ``next_attempt_at = now()`` so the
        dispatcher picks them up on the next sweep.

        Returns the number of reclaimed rows.
        """
        result = await self._execute(
            """
            UPDATE webhook_deliveries
            SET status = 'pending',
                locked_at = NULL,
                next_attempt_at = now(),
                updated_at = now()
            WHERE status = 'in_progress'
              AND locked_at < $1
            """,
            locked_before,
        )
        return int(result.split()[-1])

    async def delete_old_succeeded(self, created_before: datetime) -> int:
        """Purge succeeded deliveries older than *created_before*. Returns count."""
        result = await self._execute(
            "DELETE FROM webhook_deliveries WHERE status = 'succeeded' AND created_at < $1",
            created_before,
        )
        return int(result.split()[-1])

