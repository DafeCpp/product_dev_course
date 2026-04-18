"""Unit tests for backend_common.script_runner.executor."""
from __future__ import annotations

import pytest

from backend_common.script_runner.executor import ExecutionResult, execute_script


class TestExecuteScript:
    @pytest.mark.asyncio
    async def test_execute_python_success(self) -> None:
        result = await execute_script(
            script_body="print('hello')",
            script_type="python",
            parameters={},
        )
        assert result.exit_code == 0
        assert result.stdout == "hello\n"

    @pytest.mark.asyncio
    async def test_execute_python_error(self) -> None:
        result = await execute_script(
            script_body="import sys; sys.exit(1)",
            script_type="python",
            parameters={},
        )
        assert result.exit_code == 1

    @pytest.mark.asyncio
    async def test_execute_bash_success(self) -> None:
        result = await execute_script(
            script_body="echo test",
            script_type="bash",
            parameters={},
        )
        assert result.exit_code == 0
        assert result.stdout == "test\n"

    @pytest.mark.asyncio
    async def test_execute_timeout(self) -> None:
        result = await execute_script(
            script_body="sleep 100",
            script_type="bash",
            parameters={},
            timeout_sec=1,
        )
        # exit_code should be -1 (our sentinel) or non-zero from SIGTERM/SIGKILL
        assert result.exit_code != 0

    @pytest.mark.asyncio
    async def test_execute_parameters_in_env(self) -> None:
        result = await execute_script(
            script_body="import os; print(os.environ.get('PARAM_FOO', ''))",
            script_type="python",
            parameters={"foo": "bar"},
        )
        assert result.exit_code == 0
        assert result.stdout == "bar\n"

    @pytest.mark.asyncio
    async def test_execute_invalid_script_type(self) -> None:
        with pytest.raises(ValueError, match="Unsupported script_type"):
            await execute_script(
                script_body="puts 'hello'",
                script_type="ruby",
                parameters={},
            )

    @pytest.mark.asyncio
    async def test_execute_python_stderr(self) -> None:
        result = await execute_script(
            script_body="import sys; sys.stderr.write('err\\n')",
            script_type="python",
            parameters={},
        )
        assert result.exit_code == 0
        assert "err" in result.stderr

    @pytest.mark.asyncio
    async def test_execute_isolated_env(self) -> None:
        """Subprocess should not see host env vars other than PARAM_* and PATH."""
        result = await execute_script(
            script_body="import os; print(os.environ.get('HOME', 'NOT_SET'))",
            script_type="python",
            parameters={},
        )
        assert result.exit_code == 0
        assert result.stdout.strip() == "NOT_SET"

    @pytest.mark.asyncio
    async def test_execute_result_is_named_tuple(self) -> None:
        result = await execute_script(
            script_body="pass",
            script_type="python",
            parameters={},
        )
        assert isinstance(result, ExecutionResult)
        assert isinstance(result.exit_code, int)
        assert isinstance(result.stdout, str)
        assert isinstance(result.stderr, str)
