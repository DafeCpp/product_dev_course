"""Execution dispatcher: creates execution records and publishes to RabbitMQ."""
from __future__ import annotations

import json
from typing import Any
from uuid import UUID

import aio_pika  # type: ignore[import-untyped]
import structlog

from script_service.domain.models import ExecutionStatus, ScriptExecution
from script_service.repositories.executions import ExecutionRepository
from script_service.repositories.scripts import ScriptRepository

logger = structlog.get_logger(__name__)


class ExecutionNotFoundError(Exception):
    """Raised when execution is not found."""


class ScriptNotFoundError(Exception):
    """Raised when script is not found during dispatch."""


class ExecutionDispatcher:
    """Dispatch script executions via RabbitMQ."""

    def __init__(
        self,
        execution_repo: ExecutionRepository,
        script_repo: ScriptRepository,
        rabbitmq_url: str,
    ) -> None:
        self._execution_repo = execution_repo
        self._script_repo = script_repo
        self._rabbitmq_url = rabbitmq_url
        self._connection: Any = None
        self._channel: Any = None

    async def _ensure_connection(self) -> Any:
        """Lazily connect to RabbitMQ on first use."""
        if self._connection is None or self._connection.is_closed:
            self._connection = await aio_pika.connect_robust(self._rabbitmq_url)
        if self._channel is None or self._channel.is_closed:
            self._channel = await self._connection.channel()
        return self._channel

    async def close(self) -> None:
        """Close RabbitMQ connection."""
        if self._connection is not None and not self._connection.is_closed:
            await self._connection.close()

    async def _publish(self, routing_key: str, payload: dict[str, Any]) -> None:
        channel = await self._ensure_connection()
        await channel.default_exchange.publish(
            aio_pika.Message(
                body=json.dumps(payload).encode(),
                content_type="application/json",
                delivery_mode=aio_pika.DeliveryMode.PERSISTENT,
            ),
            routing_key=routing_key,
        )

    async def execute(
        self,
        user_id: UUID,
        script_id: UUID,
        parameters: dict[str, Any],
        target_instance: str | None = None,
    ) -> ScriptExecution:
        script = await self._script_repo.get_by_id(script_id)
        if script is None:
            raise ScriptNotFoundError(f"Script {script_id} not found")

        execution = await self._execution_repo.create(
            script_id=script_id,
            parameters=parameters,
            requested_by=user_id,
            target_instance=target_instance,
        )

        routing_key = f"script.execute.{script.target_service}"
        payload = {
            "execution_id": str(execution.id),
            "script_id": str(script_id),
            "script_body": script.script_body,
            "script_type": str(script.script_type),
            "parameters": parameters,
            "timeout_sec": script.timeout_sec,
            "target_instance": target_instance,
            "requested_by": str(user_id),
        }
        try:
            await self._publish(routing_key, payload)
            logger.info(
                "execution_dispatched",
                execution_id=str(execution.id),
                routing_key=routing_key,
            )
        except Exception as exc:
            logger.error(
                "execution_dispatch_failed",
                execution_id=str(execution.id),
                error=str(exc),
            )
            await self._execution_repo.update_status(
                execution.id,
                ExecutionStatus.failed,
                error_message=f"Failed to dispatch to RabbitMQ: {exc}",
            )
            raise

        return execution

    async def cancel(self, user_id: UUID, execution_id: UUID) -> ScriptExecution:
        execution = await self._execution_repo.get_by_id(execution_id)
        if execution is None:
            raise ExecutionNotFoundError(f"Execution {execution_id} not found")

        updated = await self._execution_repo.update_status(
            execution_id, ExecutionStatus.cancelled
        )
        assert updated is not None

        script = await self._script_repo.get_by_id(execution.script_id)
        if script is not None:
            routing_key = f"script.cancel.{script.target_service}"
            payload = {
                "execution_id": str(execution_id),
                "script_id": str(execution.script_id),
                "cancelled_by": str(user_id),
            }
            try:
                await self._publish(routing_key, payload)
                logger.info(
                    "execution_cancelled",
                    execution_id=str(execution_id),
                    routing_key=routing_key,
                )
            except Exception as exc:
                logger.warning(
                    "execution_cancel_publish_failed",
                    execution_id=str(execution_id),
                    error=str(exc),
                )

        return updated

    async def get_execution(self, id: UUID) -> ScriptExecution:
        execution = await self._execution_repo.get_by_id(id)
        if execution is None:
            raise ExecutionNotFoundError(f"Execution {id} not found")
        return execution

    async def list_executions(
        self,
        *,
        script_id: UUID | None = None,
        status: ExecutionStatus | None = None,
        requested_by: UUID | None = None,
        limit: int = 50,
        offset: int = 0,
    ) -> list[ScriptExecution]:
        return await self._execution_repo.list(
            script_id=script_id,
            status=status,
            requested_by=requested_by,
            limit=limit,
            offset=offset,
        )
