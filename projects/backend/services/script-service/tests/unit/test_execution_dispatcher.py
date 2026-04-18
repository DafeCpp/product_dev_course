"""Unit tests for ExecutionDispatcher service layer."""
from __future__ import annotations

from datetime import datetime, timezone
from unittest.mock import AsyncMock, MagicMock, patch
from uuid import UUID, uuid4

import pytest

from script_service.domain.models import ExecutionStatus, Script, ScriptExecution, ScriptType
from script_service.services.execution_dispatcher import (
    ExecutionDispatcher,
    ExecutionNotFoundError,
    ScriptNotFoundError,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

_NOW = datetime(2024, 1, 1, tzinfo=timezone.utc)
_USER_ID = UUID("550e8400-e29b-41d4-a716-446655440001")
_SCRIPT_ID = uuid4()
_EXECUTION_ID = uuid4()


def _make_script(**overrides) -> Script:
    defaults = dict(
        id=_SCRIPT_ID,
        name="test-script",
        description=None,
        target_service="experiment-service",
        script_type=ScriptType.python,
        script_body="print('ok')",
        parameters_schema={},
        timeout_sec=30,
        is_active=True,
        created_by=_USER_ID,
        created_at=_NOW,
        updated_at=_NOW,
    )
    defaults.update(overrides)
    return Script(**defaults)


def _make_execution(**overrides) -> ScriptExecution:
    defaults = dict(
        id=_EXECUTION_ID,
        script_id=_SCRIPT_ID,
        status=ExecutionStatus.pending,
        parameters={},
        target_instance=None,
        requested_by=_USER_ID,
        started_at=None,
        finished_at=None,
        exit_code=None,
        stdout=None,
        stderr=None,
        error_message=None,
        created_at=_NOW,
        updated_at=_NOW,
    )
    defaults.update(overrides)
    return ScriptExecution(**defaults)


def _make_dispatcher(
    script_repo_overrides: dict | None = None,
    execution_repo_overrides: dict | None = None,
    mock_exchange: MagicMock | None = None,
) -> tuple[ExecutionDispatcher, MagicMock, MagicMock, MagicMock]:
    """Build dispatcher with mocked repos and RabbitMQ channel."""
    script_repo = AsyncMock()
    if script_repo_overrides:
        for name, value in script_repo_overrides.items():
            getattr(script_repo, name).return_value = value

    execution_repo = AsyncMock()
    if execution_repo_overrides:
        for name, value in execution_repo_overrides.items():
            getattr(execution_repo, name).return_value = value

    if mock_exchange is None:
        mock_exchange = MagicMock()
        mock_exchange.publish = AsyncMock()

    mock_channel = MagicMock()
    mock_channel.is_closed = False
    mock_channel.default_exchange = mock_exchange

    mock_connection = MagicMock()
    mock_connection.is_closed = False
    mock_connection.channel = AsyncMock(return_value=mock_channel)

    dispatcher = ExecutionDispatcher(
        execution_repo=execution_repo,
        script_repo=script_repo,
        rabbitmq_url="amqp://guest:guest@localhost/",
    )

    return dispatcher, script_repo, execution_repo, mock_exchange, mock_connection


# ===========================================================================
# TestExecutionDispatcher
# ===========================================================================

class TestExecutionDispatcher:
    async def test_execute_creates_execution_and_publishes(self):
        script = _make_script()
        execution = _make_execution()

        dispatcher, script_repo, execution_repo, mock_exchange, mock_connection = (
            _make_dispatcher(
                script_repo_overrides={"get_by_id": script},
                execution_repo_overrides={"create": execution},
            )
        )

        with patch(
            "aio_pika.connect_robust", new=AsyncMock(return_value=mock_connection)
        ):
            result = await dispatcher.execute(
                user_id=_USER_ID,
                script_id=_SCRIPT_ID,
                parameters={},
            )

        execution_repo.create.assert_awaited_once_with(
            script_id=_SCRIPT_ID,
            parameters={},
            requested_by=_USER_ID,
            target_instance=None,
        )
        mock_exchange.publish.assert_awaited_once()
        assert result is execution

    async def test_execute_script_not_found_raises_script_not_found_error(self):
        dispatcher, script_repo, execution_repo, mock_exchange, mock_connection = (
            _make_dispatcher(script_repo_overrides={"get_by_id": None})
        )

        with patch(
            "aio_pika.connect_robust", new=AsyncMock(return_value=mock_connection)
        ):
            with pytest.raises(ScriptNotFoundError):
                await dispatcher.execute(
                    user_id=_USER_ID,
                    script_id=_SCRIPT_ID,
                    parameters={},
                )

    async def test_execute_publish_fails_marks_execution_failed(self):
        script = _make_script()
        execution = _make_execution()

        dispatcher, script_repo, execution_repo, mock_exchange, mock_connection = (
            _make_dispatcher(
                script_repo_overrides={"get_by_id": script},
                execution_repo_overrides={"create": execution},
            )
        )
        mock_exchange.publish.side_effect = Exception("RabbitMQ is down")

        with patch(
            "aio_pika.connect_robust", new=AsyncMock(return_value=mock_connection)
        ):
            with pytest.raises(Exception, match="RabbitMQ is down"):
                await dispatcher.execute(
                    user_id=_USER_ID,
                    script_id=_SCRIPT_ID,
                    parameters={},
                )

        execution_repo.update_status.assert_awaited_once_with(
            execution.id,
            ExecutionStatus.failed,
            error_message=pytest.approx(
                "Failed to dispatch to RabbitMQ: RabbitMQ is down",
                abs=0,
            ),
        )

    async def test_cancel_execution_success_updates_status_and_publishes(self):
        execution = _make_execution()
        cancelled_execution = _make_execution(status=ExecutionStatus.cancelled)
        script = _make_script()

        dispatcher, script_repo, execution_repo, mock_exchange, mock_connection = (
            _make_dispatcher(
                script_repo_overrides={"get_by_id": script},
                execution_repo_overrides={
                    "get_by_id": execution,
                    "update_status": cancelled_execution,
                },
            )
        )

        with patch(
            "aio_pika.connect_robust", new=AsyncMock(return_value=mock_connection)
        ):
            result = await dispatcher.cancel(
                user_id=_USER_ID,
                execution_id=_EXECUTION_ID,
            )

        execution_repo.update_status.assert_awaited_once_with(
            _EXECUTION_ID, ExecutionStatus.cancelled
        )
        mock_exchange.publish.assert_awaited_once()

        call_kwargs = mock_exchange.publish.call_args
        routing_key = call_kwargs.kwargs.get("routing_key") or call_kwargs.args[-1]
        assert routing_key == f"script.cancel.{script.target_service}"

        assert result is cancelled_execution

    async def test_cancel_not_found_raises_execution_not_found_error(self):
        dispatcher, _, execution_repo, mock_exchange, mock_connection = (
            _make_dispatcher(execution_repo_overrides={"get_by_id": None})
        )

        with patch(
            "aio_pika.connect_robust", new=AsyncMock(return_value=mock_connection)
        ):
            with pytest.raises(ExecutionNotFoundError):
                await dispatcher.cancel(
                    user_id=_USER_ID,
                    execution_id=_EXECUTION_ID,
                )

    async def test_cancel_publish_fails_silently(self):
        execution = _make_execution()
        cancelled_execution = _make_execution(status=ExecutionStatus.cancelled)
        script = _make_script()

        dispatcher, script_repo, execution_repo, mock_exchange, mock_connection = (
            _make_dispatcher(
                script_repo_overrides={"get_by_id": script},
                execution_repo_overrides={
                    "get_by_id": execution,
                    "update_status": cancelled_execution,
                },
            )
        )
        mock_exchange.publish.side_effect = Exception("broker unavailable")

        with patch(
            "aio_pika.connect_robust", new=AsyncMock(return_value=mock_connection)
        ):
            # Should not raise even though publish fails
            result = await dispatcher.cancel(
                user_id=_USER_ID,
                execution_id=_EXECUTION_ID,
            )

        assert result is cancelled_execution

    async def test_list_executions_delegates_to_repo(self):
        executions = [_make_execution() for _ in range(3)]

        dispatcher, _, execution_repo, _, mock_connection = _make_dispatcher(
            execution_repo_overrides={"list": executions}
        )

        with patch(
            "aio_pika.connect_robust", new=AsyncMock(return_value=mock_connection)
        ):
            result = await dispatcher.list_executions(
                script_id=_SCRIPT_ID,
                status=ExecutionStatus.pending,
            )

        execution_repo.list.assert_awaited_once_with(
            script_id=_SCRIPT_ID,
            status=ExecutionStatus.pending,
            requested_by=None,
            limit=50,
            offset=0,
        )
        assert result == executions
