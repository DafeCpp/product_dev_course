"""Unit tests for ScriptRunner and ScriptConsumer."""
from __future__ import annotations

import asyncio
import json
from contextlib import asynccontextmanager
from typing import AsyncIterator
from unittest.mock import AsyncMock, MagicMock, patch
from uuid import uuid4

import pytest

from backend_common.script_runner.consumer import ScriptConsumer
from backend_common.script_runner.executor import ExecutionResult
from backend_common.script_runner.models import CancelCommand, ExecuteCommand
from backend_common.script_runner.runner import ScriptRunner


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_execute_cmd(**overrides: object) -> ExecuteCommand:
    defaults: dict = {
        "execution_id": uuid4(),
        "script_id": uuid4(),
        "script_body": "print('hi')",
        "script_type": "python",
        "parameters": {},
        "timeout_sec": 30,
        "requested_by": uuid4(),
    }
    defaults.update(overrides)
    return ExecuteCommand.model_validate(defaults)


def _make_cancel_cmd(execution_id=None, **overrides: object) -> CancelCommand:
    defaults: dict = {
        "execution_id": execution_id or uuid4(),
        "script_id": uuid4(),
        "cancelled_by": uuid4(),
    }
    defaults.update(overrides)
    return CancelCommand.model_validate(defaults)


def _make_incoming_message(body: bytes) -> MagicMock:
    """Create a mock aio_pika IncomingMessage that supports `async with message.process()`."""
    message = MagicMock()
    message.body = body

    @asynccontextmanager
    async def _process(requeue: bool = True) -> AsyncIterator[None]:
        yield

    message.process = _process
    return message


# ---------------------------------------------------------------------------
# TestScriptRunnerOnExecute
# ---------------------------------------------------------------------------


class TestScriptRunnerOnExecute:
    @pytest.mark.asyncio
    async def test_on_execute_returns_result(self) -> None:
        """_on_execute calls execute_script and returns its ExecutionResult."""
        runner = ScriptRunner(rabbitmq_url="amqp://fake", service_name="test-svc")
        cmd = _make_execute_cmd()
        expected = ExecutionResult(exit_code=0, stdout="hi\n", stderr="")

        with patch(
            "backend_common.script_runner.runner.execute_script",
            new=AsyncMock(return_value=expected),
        ) as mock_exec:
            result = await runner._on_execute(cmd)

        assert result == expected
        mock_exec.assert_awaited_once_with(
            script_body=cmd.script_body,
            script_type=cmd.script_type,
            parameters=cmd.parameters,
            timeout_sec=cmd.timeout_sec,
        )

    @pytest.mark.asyncio
    async def test_on_execute_removes_task_from_running_tasks_on_success(self) -> None:
        """After _on_execute completes, the execution_id is removed from _running_tasks."""
        runner = ScriptRunner(rabbitmq_url="amqp://fake", service_name="test-svc")
        cmd = _make_execute_cmd()

        with patch(
            "backend_common.script_runner.runner.execute_script",
            new=AsyncMock(return_value=ExecutionResult(0, "", "")),
        ):
            await runner._on_execute(cmd)

        assert str(cmd.execution_id) not in runner._running_tasks

    @pytest.mark.asyncio
    async def test_on_execute_removes_task_from_running_tasks_on_exception(self) -> None:
        """_on_execute cleans up _running_tasks even when execute_script raises."""
        runner = ScriptRunner(rabbitmq_url="amqp://fake", service_name="test-svc")
        cmd = _make_execute_cmd()

        with patch(
            "backend_common.script_runner.runner.execute_script",
            new=AsyncMock(side_effect=RuntimeError("boom")),
        ):
            with pytest.raises(RuntimeError, match="boom"):
                await runner._on_execute(cmd)

        assert str(cmd.execution_id) not in runner._running_tasks

    @pytest.mark.asyncio
    async def test_on_cancel_cancels_running_task(self) -> None:
        """_on_cancel calls task.cancel() for a registered execution_id."""
        runner = ScriptRunner(rabbitmq_url="amqp://fake", service_name="test-svc")
        cmd = _make_execute_cmd()
        execution_key = str(cmd.execution_id)

        mock_task = MagicMock(spec=asyncio.Task)
        mock_task.done.return_value = False
        runner._running_tasks[execution_key] = mock_task

        cancel_cmd = _make_cancel_cmd(execution_id=cmd.execution_id)
        await runner._on_cancel(cancel_cmd)

        mock_task.cancel.assert_called_once()

    @pytest.mark.asyncio
    async def test_on_cancel_does_not_cancel_already_done_task(self) -> None:
        """_on_cancel skips task.cancel() when the task has already finished."""
        runner = ScriptRunner(rabbitmq_url="amqp://fake", service_name="test-svc")
        cmd = _make_execute_cmd()
        execution_key = str(cmd.execution_id)

        mock_task = MagicMock(spec=asyncio.Task)
        mock_task.done.return_value = True
        runner._running_tasks[execution_key] = mock_task

        cancel_cmd = _make_cancel_cmd(execution_id=cmd.execution_id)
        await runner._on_cancel(cancel_cmd)

        mock_task.cancel.assert_not_called()

    @pytest.mark.asyncio
    async def test_on_cancel_nonexistent_execution_noop(self) -> None:
        """_on_cancel with unknown execution_id does not raise."""
        runner = ScriptRunner(rabbitmq_url="amqp://fake", service_name="test-svc")
        cancel_cmd = _make_cancel_cmd()

        # Must not raise
        await runner._on_cancel(cancel_cmd)


# ---------------------------------------------------------------------------
# TestScriptConsumerProcessExecute
# ---------------------------------------------------------------------------


class TestScriptConsumerProcessExecute:
    def _make_consumer(
        self,
        on_execute: AsyncMock | None = None,
        on_cancel: AsyncMock | None = None,
    ) -> ScriptConsumer:
        return ScriptConsumer(
            rabbitmq_url="amqp://fake",
            service_name="test-svc",
            on_execute=on_execute or AsyncMock(return_value=ExecutionResult(0, "ok", "")),
            on_cancel=on_cancel or AsyncMock(),
        )

    @pytest.mark.asyncio
    async def test_process_execute_success(self) -> None:
        """Valid ExecuteCommand message triggers on_execute with the parsed command."""
        cmd = _make_execute_cmd()
        on_execute = AsyncMock(return_value=ExecutionResult(0, "hello", ""))
        consumer = self._make_consumer(on_execute=on_execute)

        message = _make_incoming_message(
            json.dumps(cmd.model_dump(mode="json")).encode()
        )

        # _publish_status is a no-op when _status_exchange is None (default state)
        await consumer._process_execute(message)

        # _process_execute creates a task; give the event loop a chance to run it
        await asyncio.sleep(0)

        on_execute.assert_awaited_once()
        actual_cmd: ExecuteCommand = on_execute.call_args[0][0]
        assert actual_cmd.execution_id == cmd.execution_id
        assert actual_cmd.script_id == cmd.script_id

    @pytest.mark.asyncio
    async def test_process_execute_publishes_status_when_exchange_ready(self) -> None:
        """When _status_exchange is set, _publish_status is called for each status update."""
        cmd = _make_execute_cmd()
        on_execute = AsyncMock(return_value=ExecutionResult(0, "out", ""))
        consumer = self._make_consumer(on_execute=on_execute)

        mock_exchange = AsyncMock()
        consumer._status_exchange = mock_exchange

        message = _make_incoming_message(
            json.dumps(cmd.model_dump(mode="json")).encode()
        )

        await consumer._process_execute(message)
        # Allow the spawned task to complete
        await asyncio.sleep(0)
        await asyncio.sleep(0)

        # At minimum one publish call must have happened (running + completed)
        assert mock_exchange.publish.await_count >= 1

    @pytest.mark.asyncio
    async def test_process_execute_invalid_json_does_not_call_handler(self) -> None:
        """Malformed JSON body: on_execute is never called (parse error silently consumed)."""
        on_execute = AsyncMock(return_value=ExecutionResult(0, "", ""))
        consumer = self._make_consumer(on_execute=on_execute)

        message = _make_incoming_message(b"not valid json{{")
        await consumer._process_execute(message)
        await asyncio.sleep(0)

        on_execute.assert_not_awaited()

    @pytest.mark.asyncio
    async def test_process_execute_invalid_schema_does_not_call_handler(self) -> None:
        """JSON that doesn't match ExecuteCommand schema: on_execute is never called."""
        on_execute = AsyncMock(return_value=ExecutionResult(0, "", ""))
        consumer = self._make_consumer(on_execute=on_execute)

        message = _make_incoming_message(json.dumps({"unexpected_field": 42}).encode())
        await consumer._process_execute(message)
        await asyncio.sleep(0)

        on_execute.assert_not_awaited()

    @pytest.mark.asyncio
    async def test_process_cancel_success(self) -> None:
        """Valid CancelCommand message triggers on_cancel with the parsed command."""
        cmd = _make_execute_cmd()
        cancel_cmd = _make_cancel_cmd(execution_id=cmd.execution_id)
        on_cancel = AsyncMock()
        consumer = self._make_consumer(on_cancel=on_cancel)

        message = _make_incoming_message(
            json.dumps(cancel_cmd.model_dump(mode="json")).encode()
        )

        await consumer._process_cancel(message)

        on_cancel.assert_awaited_once()
        actual_cmd: CancelCommand = on_cancel.call_args[0][0]
        assert actual_cmd.execution_id == cancel_cmd.execution_id

    @pytest.mark.asyncio
    async def test_process_cancel_invalid_json_does_not_call_handler(self) -> None:
        """Malformed JSON in cancel message: on_cancel is never called."""
        on_cancel = AsyncMock()
        consumer = self._make_consumer(on_cancel=on_cancel)

        message = _make_incoming_message(b"[broken json")
        await consumer._process_cancel(message)

        on_cancel.assert_not_awaited()

    @pytest.mark.asyncio
    async def test_process_cancel_handler_exception_is_swallowed(self) -> None:
        """Exception raised inside on_cancel does not propagate out of _process_cancel."""
        on_cancel = AsyncMock(side_effect=RuntimeError("cancel boom"))
        consumer = self._make_consumer(on_cancel=on_cancel)

        cancel_cmd = _make_cancel_cmd()
        message = _make_incoming_message(
            json.dumps(cancel_cmd.model_dump(mode="json")).encode()
        )

        # Must not raise
        await consumer._process_cancel(message)

    @pytest.mark.asyncio
    async def test_process_execute_exit_code_nonzero_publishes_failed_status(self) -> None:
        """Non-zero exit code from on_execute results in a 'failed' status report."""
        cmd = _make_execute_cmd()
        on_execute = AsyncMock(return_value=ExecutionResult(1, "", "error text"))
        consumer = self._make_consumer(on_execute=on_execute)

        mock_exchange = AsyncMock()
        consumer._status_exchange = mock_exchange

        message = _make_incoming_message(
            json.dumps(cmd.model_dump(mode="json")).encode()
        )

        await consumer._process_execute(message)
        await asyncio.sleep(0)
        await asyncio.sleep(0)

        # Collect all published bodies
        published_bodies: list[str] = []
        for call in mock_exchange.publish.await_args_list:
            aio_msg = call[0][0]
            published_bodies.append(aio_msg.body.decode())

        statuses = [json.loads(b)["status"] for b in published_bodies]
        assert "failed" in statuses

    @pytest.mark.asyncio
    async def test_process_execute_exit_code_minus1_publishes_timeout_status(self) -> None:
        """exit_code == -1 from on_execute results in a 'timeout' status report."""
        cmd = _make_execute_cmd()
        on_execute = AsyncMock(return_value=ExecutionResult(-1, "", ""))
        consumer = self._make_consumer(on_execute=on_execute)

        mock_exchange = AsyncMock()
        consumer._status_exchange = mock_exchange

        message = _make_incoming_message(
            json.dumps(cmd.model_dump(mode="json")).encode()
        )

        await consumer._process_execute(message)
        await asyncio.sleep(0)
        await asyncio.sleep(0)

        published_bodies: list[str] = []
        for call in mock_exchange.publish.await_args_list:
            aio_msg = call[0][0]
            published_bodies.append(aio_msg.body.decode())

        statuses = [json.loads(b)["status"] for b in published_bodies]
        assert "timeout" in statuses
