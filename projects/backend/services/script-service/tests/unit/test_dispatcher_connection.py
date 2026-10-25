"""Unit tests for ExecutionDispatcher RabbitMQ connection handling, models and entrypoint."""
from __future__ import annotations

from datetime import UTC, datetime
from unittest.mock import AsyncMock, MagicMock, patch
from uuid import uuid4

from script_service import main as main_module
from script_service.domain.models import ExecutionStatus, Script, ScriptExecution, ScriptType
from script_service.services.execution_dispatcher import ExecutionDispatcher

_NOW = datetime(2024, 1, 1, tzinfo=UTC)


def _dispatcher() -> ExecutionDispatcher:
    return ExecutionDispatcher(AsyncMock(), AsyncMock(), "amqp://test")


def _open_connection() -> MagicMock:
    connection = MagicMock()
    connection.is_closed = False
    connection.channel = AsyncMock(return_value=MagicMock(is_closed=False))
    connection.close = AsyncMock()
    return connection


async def test_ensure_connection_connects_once_and_reuses():
    dispatcher = _dispatcher()
    connection = _open_connection()

    with patch(
        "script_service.services.execution_dispatcher.aio_pika.connect_robust",
        AsyncMock(return_value=connection),
    ) as connect:
        first = await dispatcher._ensure_connection()
        second = await dispatcher._ensure_connection()

    assert first is second
    connect.assert_awaited_once_with("amqp://test")
    connection.channel.assert_awaited_once()


async def test_ensure_connection_reopens_closed_connection_and_channel():
    dispatcher = _dispatcher()
    stale_connection = MagicMock(is_closed=True)
    stale_channel = MagicMock(is_closed=True)
    dispatcher._connection = stale_connection
    dispatcher._channel = stale_channel
    fresh = _open_connection()

    with patch(
        "script_service.services.execution_dispatcher.aio_pika.connect_robust",
        AsyncMock(return_value=fresh),
    ):
        channel = await dispatcher._ensure_connection()

    assert dispatcher._connection is fresh
    assert channel is fresh.channel.return_value
    assert channel is not stale_channel


async def test_ensure_connection_reopens_only_closed_channel():
    dispatcher = _dispatcher()
    connection = _open_connection()
    dispatcher._connection = connection
    dispatcher._channel = MagicMock(is_closed=True)

    with patch(
        "script_service.services.execution_dispatcher.aio_pika.connect_robust", AsyncMock()
    ) as connect:
        channel = await dispatcher._ensure_connection()

    connect.assert_not_awaited()
    assert channel is connection.channel.return_value


async def test_close_closes_open_connection():
    dispatcher = _dispatcher()
    connection = _open_connection()
    dispatcher._connection = connection

    await dispatcher.close()

    connection.close.assert_awaited_once()


async def test_close_skips_missing_or_closed_connection():
    dispatcher = _dispatcher()
    await dispatcher.close()  # never connected

    closed = MagicMock(is_closed=True)
    closed.close = AsyncMock()
    dispatcher._connection = closed
    await dispatcher.close()

    closed.close.assert_not_awaited()


async def test_cancel_without_script_skips_publish():
    dispatcher = _dispatcher()
    execution = ScriptExecution(
        id=uuid4(),
        script_id=uuid4(),
        status=ExecutionStatus.running,
        parameters={},
        target_instance=None,
        requested_by=uuid4(),
        started_at=None,
        finished_at=None,
        exit_code=None,
        stdout=None,
        stderr=None,
        error_message=None,
        created_at=_NOW,
        updated_at=_NOW,
    )
    dispatcher._execution_repo.get_by_id.return_value = execution
    dispatcher._execution_repo.update_status.return_value = execution
    dispatcher._script_repo.get_by_id.return_value = None
    dispatcher._publish = AsyncMock()  # type: ignore[method-assign]

    result = await dispatcher.cancel(uuid4(), execution.id)

    assert result is execution
    dispatcher._publish.assert_not_awaited()


def test_from_row_accepts_already_decoded_json():
    script_id, user_id = uuid4(), uuid4()
    script = Script.from_row(
        {
            "id": script_id,
            "name": "n",
            "description": None,
            "target_service": "svc",
            "script_type": "python",
            "script_body": "x",
            "parameters_schema": {"a": 1},
            "timeout_sec": 5,
            "is_active": True,
            "created_by": user_id,
            "created_at": _NOW,
            "updated_at": _NOW,
        }
    )
    assert script.parameters_schema == {"a": 1}
    assert script.script_type == ScriptType.python

    execution = ScriptExecution.from_row(
        {
            "id": uuid4(),
            "script_id": script_id,
            "status": "pending",
            "parameters": {"k": 1},
            "target_instance": None,
            "requested_by": user_id,
            "started_at": None,
            "finished_at": None,
            "exit_code": None,
            "stdout": None,
            "stderr": None,
            "error_message": None,
            "created_at": _NOW,
            "updated_at": _NOW,
        }
    )
    assert execution.parameters == {"k": 1}


def test_main_runs_app_with_service_settings():
    with patch.object(main_module.web, "run_app") as run_app, patch.object(
        main_module, "create_app", return_value="app"
    ):
        main_module.main()

    run_app.assert_called_once_with(
        "app", host=main_module.settings.host, port=main_module.settings.port, access_log=None
    )
