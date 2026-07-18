from __future__ import annotations

import json
from unittest.mock import AsyncMock, MagicMock, patch
from uuid import uuid4

import pytest

from backend_common.script_runner.consumer import ScriptConsumer
from backend_common.script_runner.models import StatusReport


def _consumer() -> ScriptConsumer:
    return ScriptConsumer(
        "amqp://guest:guest@localhost/",
        "service-a",
        AsyncMock(),
        AsyncMock(),
    )


@pytest.mark.asyncio
async def test_start_declares_and_consumes_service_queues() -> None:
    connection = AsyncMock()
    channel = AsyncMock()
    connection.channel.return_value = channel
    execute_queue = AsyncMock()
    cancel_queue = AsyncMock()
    channel.declare_queue.side_effect = [execute_queue, cancel_queue]
    consumer = _consumer()

    with patch("backend_common.script_runner.consumer.aio_pika.connect_robust", return_value=connection):
        await consumer.start()

    channel.set_qos.assert_awaited_once_with(prefetch_count=1)
    channel.declare_exchange.assert_awaited_once()
    assert channel.declare_queue.await_args_list[0].args == ("script.execute.service-a",)
    assert channel.declare_queue.await_args_list[1].args == ("script.cancel.service-a",)
    execute_queue.consume.assert_awaited_once_with(consumer._process_execute)
    cancel_queue.consume.assert_awaited_once_with(consumer._process_cancel)


@pytest.mark.asyncio
async def test_status_publish_and_stop_handle_ready_and_unready_connection() -> None:
    consumer = _consumer()
    report = StatusReport(execution_id=uuid4(), status="completed", exit_code=0)

    await consumer._publish_status(report)

    exchange = AsyncMock()
    consumer._status_exchange = exchange
    await consumer._publish_status(report)
    assert exchange.publish.await_count == 1
    assert exchange.publish.await_args.kwargs["routing_key"] == f"script.status.{report.execution_id}"

    connection = AsyncMock()
    connection.is_closed = False
    consumer._connection = connection
    await consumer.stop()
    connection.close.assert_awaited_once()

    connection.is_closed = True
    await consumer.stop()
    connection.close.assert_awaited_once()


@pytest.mark.asyncio
async def test_cancel_message_dispatches_valid_command_and_ignores_invalid_payload() -> None:
    on_cancel = AsyncMock()
    consumer = ScriptConsumer("amqp://guest:guest@localhost/", "service-a", AsyncMock(), on_cancel)
    message = MagicMock()
    message.process.return_value.__aenter__ = AsyncMock()
    message.process.return_value.__aexit__ = AsyncMock()
    message.body = b'{"execution_id": "not-a-uuid"}'
    await consumer._process_cancel(message)
    on_cancel.assert_not_awaited()

    execution_id = uuid4()
    message.body = json.dumps(
        {
            "execution_id": str(execution_id),
            "script_id": str(uuid4()),
            "cancelled_by": str(uuid4()),
        }
    ).encode()
    await consumer._process_cancel(message)
    assert on_cancel.await_args.args[0].execution_id == execution_id
