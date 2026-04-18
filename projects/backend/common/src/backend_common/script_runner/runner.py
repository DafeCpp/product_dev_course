"""ScriptRunner: combines Consumer and Executor into a single embeddable component."""
from __future__ import annotations

import asyncio

from backend_common.script_runner.consumer import ScriptConsumer
from backend_common.script_runner.executor import ExecutionResult, execute_script
from backend_common.script_runner.models import CancelCommand, ExecuteCommand


class ScriptRunner:
    """Combines ScriptConsumer and execute_script into a ready-to-use component.

    Embed in any service via start()/stop():

        runner = ScriptRunner(rabbitmq_url=..., service_name="my-service")
        app.on_startup.append(lambda _app: runner.start())
        app.on_cleanup.append(lambda _app: runner.stop())
    """

    def __init__(
        self,
        rabbitmq_url: str,
        service_name: str,
        max_concurrent: int = 3,
    ) -> None:
        self._running_tasks: dict[str, asyncio.Task[None]] = {}
        self._consumer = ScriptConsumer(
            rabbitmq_url=rabbitmq_url,
            service_name=service_name,
            on_execute=self._on_execute,
            on_cancel=self._on_cancel,
            max_concurrent=max_concurrent,
        )

    async def _on_execute(self, cmd: ExecuteCommand) -> ExecutionResult:
        task: asyncio.Task[ExecutionResult] = asyncio.current_task()  # type: ignore[assignment]
        execution_key = str(cmd.execution_id)

        # Register the current task so it can be cancelled via _on_cancel
        # We wrap the coroutine so we can store the task reference before it runs.
        # Since we are already inside the task at this point, we store current_task().
        if task is not None:
            self._running_tasks[execution_key] = task  # type: ignore[assignment]

        try:
            return await execute_script(
                script_body=cmd.script_body,
                script_type=cmd.script_type,
                parameters=cmd.parameters,
                timeout_sec=cmd.timeout_sec,
            )
        finally:
            self._running_tasks.pop(execution_key, None)

    async def _on_cancel(self, cmd: CancelCommand) -> None:
        task = self._running_tasks.get(str(cmd.execution_id))
        if task is not None and not task.done():
            task.cancel()

    async def start(self) -> None:
        """Connect to RabbitMQ and start consuming."""
        await self._consumer.start()

    async def stop(self) -> None:
        """Stop consuming and close connection."""
        await self._consumer.stop()
