"""Script executor: runs scripts in isolated subprocesses."""
from __future__ import annotations

import asyncio
import os
import sys
from typing import Any, NamedTuple


class ExecutionResult(NamedTuple):
    exit_code: int
    stdout: str
    stderr: str


_SCRIPT_COMMANDS: dict[str, list[str]] = {
    "python": [sys.executable, "-c"],
    "bash": ["bash", "-c"],
    "javascript": ["node", "-e"],
}


async def execute_script(
    script_body: str,
    script_type: str,
    parameters: dict[str, Any],
    timeout_sec: int = 30,
) -> ExecutionResult:
    """Execute a script in a subprocess.

    Parameters are passed as environment variables: PARAM_<KEY>=<VALUE>.
    The subprocess runs with an isolated environment (only PARAM_* and PATH).

    On timeout: sends SIGTERM, waits 2 seconds, then SIGKILL if still running.
    Returns exit_code=-1 on timeout.
    """
    cmd_prefix = _SCRIPT_COMMANDS.get(script_type)
    if cmd_prefix is None:
        raise ValueError(
            f"Unsupported script_type: {script_type!r}. "
            f"Supported types: {list(_SCRIPT_COMMANDS)}"
        )

    env: dict[str, str] = {"PATH": os.environ.get("PATH", "")}
    for key, value in parameters.items():
        env[f"PARAM_{key.upper()}"] = str(value)

    proc = await asyncio.create_subprocess_exec(
        *cmd_prefix,
        script_body,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
        env=env,
    )

    try:
        stdout_bytes, stderr_bytes = await asyncio.wait_for(
            proc.communicate(),
            timeout=timeout_sec,
        )
        return ExecutionResult(
            exit_code=proc.returncode if proc.returncode is not None else -1,
            stdout=stdout_bytes.decode(errors="replace"),
            stderr=stderr_bytes.decode(errors="replace"),
        )
    except asyncio.TimeoutError:
        # Graceful shutdown: SIGTERM first, SIGKILL after 2 seconds
        try:
            proc.terminate()
        except ProcessLookupError:
            pass

        try:
            await asyncio.wait_for(proc.wait(), timeout=2.0)
        except asyncio.TimeoutError:
            try:
                proc.kill()
            except ProcessLookupError:
                pass
            await proc.wait()

        # Collect whatever output was produced before the kill
        stdout_data = b""
        stderr_data = b""
        if proc.stdout is not None:
            try:
                stdout_data = await asyncio.wait_for(proc.stdout.read(), timeout=1.0)
            except (asyncio.TimeoutError, Exception):
                pass
        if proc.stderr is not None:
            try:
                stderr_data = await asyncio.wait_for(proc.stderr.read(), timeout=1.0)
            except (asyncio.TimeoutError, Exception):
                pass

        return ExecutionResult(
            exit_code=-1,
            stdout=stdout_data.decode(errors="replace"),
            stderr=stderr_data.decode(errors="replace"),
        )
