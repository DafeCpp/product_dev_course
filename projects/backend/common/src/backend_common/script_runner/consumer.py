"""RabbitMQ consumer for script execution commands."""
from __future__ import annotations

import asyncio
import json
from typing import Awaitable, Callable

import aio_pika  # type: ignore[import-untyped]
import structlog

from backend_common.script_runner.executor import ExecutionResult
from backend_common.script_runner.models import CancelCommand, ExecuteCommand, StatusReport

logger = structlog.get_logger(__name__)


class ScriptConsumer:
    """Listens to RabbitMQ queues and dispatches script executions.

    Queues consumed:
      - script.execute.<service_name>  — ExecuteCommand messages
      - script.cancel.<service_name>   — CancelCommand messages

    Status reports are published to exchange ``script.status`` with
    routing_key ``script.status.<execution_id>``.
    """

    def __init__(
        self,
        rabbitmq_url: str,
        service_name: str,
        on_execute: Callable[[ExecuteCommand], Awaitable[ExecutionResult]],
        on_cancel: Callable[[CancelCommand], Awaitable[None]],
        max_concurrent: int = 3,
    ) -> None:
        self._rabbitmq_url = rabbitmq_url
        self._service_name = service_name
        self._on_execute = on_execute
        self._on_cancel = on_cancel
        self._semaphore = asyncio.Semaphore(max_concurrent)
        self._connection: aio_pika.abc.AbstractRobustConnection | None = None
        self._channel: aio_pika.abc.AbstractChannel | None = None
        self._status_exchange: aio_pika.abc.AbstractExchange | None = None

    async def start(self) -> None:
        """Connect to RabbitMQ and start consuming queues."""
        self._connection = await aio_pika.connect_robust(self._rabbitmq_url)
        self._channel = await self._connection.channel()
        await self._channel.set_qos(prefetch_count=1)

        # Declare status exchange (fanout-style, consumers use routing key)
        self._status_exchange = await self._channel.declare_exchange(
            "script.status",
            aio_pika.ExchangeType.TOPIC,
            durable=True,
        )

        # Declare and consume execute queue
        execute_queue_name = f"script.execute.{self._service_name}"
        execute_queue = await self._channel.declare_queue(execute_queue_name, durable=True)
        await execute_queue.consume(self._process_execute)

        # Declare and consume cancel queue
        cancel_queue_name = f"script.cancel.{self._service_name}"
        cancel_queue = await self._channel.declare_queue(cancel_queue_name, durable=True)
        await cancel_queue.consume(self._process_cancel)

        logger.info(
            "script_consumer_started",
            service=self._service_name,
            execute_queue=execute_queue_name,
            cancel_queue=cancel_queue_name,
        )

    async def stop(self) -> None:
        """Stop consuming and close connection."""
        if self._connection is not None and not self._connection.is_closed:
            await self._connection.close()
            logger.info("script_consumer_stopped", service=self._service_name)

    async def _publish_status(self, report: StatusReport) -> None:
        if self._status_exchange is None:
            logger.warning("status_exchange_not_ready", execution_id=str(report.execution_id))
            return
        routing_key = f"script.status.{report.execution_id}"
        await self._status_exchange.publish(
            aio_pika.Message(
                body=report.model_dump_json().encode(),
                content_type="application/json",
                delivery_mode=aio_pika.DeliveryMode.PERSISTENT,
            ),
            routing_key=routing_key,
        )

    async def _process_execute(self, message: aio_pika.IncomingMessage) -> None:
        """Handle an ExecuteCommand message."""
        async with message.process(requeue=False):
            try:
                payload = json.loads(message.body)
                cmd = ExecuteCommand.model_validate(payload)
            except Exception as exc:
                logger.error("execute_message_parse_error", error=str(exc))
                return

            log = logger.bind(execution_id=str(cmd.execution_id), script_id=str(cmd.script_id))

            # Publish 'running' status before acquiring semaphore to signal intent,
            # but we actually wait for the semaphore before running.
            async def _run() -> None:
                async with self._semaphore:
                    await self._publish_status(
                        StatusReport(execution_id=cmd.execution_id, status="running")
                    )
                    log.info("execution_started")
                    try:
                        result = await self._on_execute(cmd)
                    except asyncio.CancelledError:
                        await self._publish_status(
                            StatusReport(
                                execution_id=cmd.execution_id,
                                status="cancelled",
                                error_message="Task was cancelled",
                            )
                        )
                        log.info("execution_cancelled")
                        return
                    except Exception as exc:
                        await self._publish_status(
                            StatusReport(
                                execution_id=cmd.execution_id,
                                status="failed",
                                error_message=str(exc),
                            )
                        )
                        log.exception("execution_failed_unexpected", error=str(exc))
                        return

                    if result.exit_code == -1:
                        status = "timeout"
                    elif result.exit_code == 0:
                        status = "completed"
                    else:
                        status = "failed"

                    await self._publish_status(
                        StatusReport(
                            execution_id=cmd.execution_id,
                            status=status,
                            exit_code=result.exit_code,
                            stdout=result.stdout or None,
                            stderr=result.stderr or None,
                        )
                    )
                    log.info("execution_finished", status=status, exit_code=result.exit_code)

            asyncio.create_task(_run())

    async def _process_cancel(self, message: aio_pika.IncomingMessage) -> None:
        """Handle a CancelCommand message."""
        async with message.process(requeue=False):
            try:
                payload = json.loads(message.body)
                cmd = CancelCommand.model_validate(payload)
            except Exception as exc:
                logger.error("cancel_message_parse_error", error=str(exc))
                return

            logger.info("cancel_received", execution_id=str(cmd.execution_id))
            try:
                await self._on_cancel(cmd)
            except Exception as exc:
                logger.error(
                    "cancel_handler_error",
                    execution_id=str(cmd.execution_id),
                    error=str(exc),
                )
