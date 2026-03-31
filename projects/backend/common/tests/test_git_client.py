"""Unit tests for backend_common.script_runner.git_client."""
from __future__ import annotations

import asyncio
from pathlib import Path
from unittest.mock import AsyncMock, MagicMock, patch

import pytest

from backend_common.script_runner.git_client import GitClient, GitError


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_proc(returncode: int = 0, stdout: bytes = b"", stderr: bytes = b"") -> MagicMock:
    """Build a mock subprocess whose communicate() returns fixed output."""
    proc = MagicMock()
    proc.returncode = returncode
    proc.communicate = AsyncMock(return_value=(stdout, stderr))
    proc.kill = MagicMock()
    proc.wait = AsyncMock(return_value=None)
    return proc


# ---------------------------------------------------------------------------
# clone_or_fetch
# ---------------------------------------------------------------------------

class TestCloneOrFetch:
    @pytest.mark.asyncio
    async def test_clone_when_directory_absent(self, tmp_path: Path) -> None:
        cache_dir = str(tmp_path / "cache")
        client = GitClient(
            repo_url="https://example.com/repo.git",
            cache_dir=cache_dir,
        )
        # work_dir must NOT exist so that clone path is taken
        assert not client._work_dir.exists()

        proc = _make_proc()
        with patch("asyncio.create_subprocess_exec", return_value=proc) as mock_exec:
            await client.clone_or_fetch()

        calls = [call.args for call in mock_exec.call_args_list]
        assert any(call[0] == "git" and call[1] == "clone" for call in calls), (
            f"Expected a 'git clone' call, got: {calls}"
        )
        # fetch must NOT have been called
        assert not any(call[1] == "fetch" for call in calls)

    @pytest.mark.asyncio
    async def test_fetch_when_directory_exists(self, tmp_path: Path) -> None:
        cache_dir = str(tmp_path / "cache")
        client = GitClient(
            repo_url="https://example.com/repo.git",
            cache_dir=cache_dir,
        )
        # Pre-create the work_dir so GitClient sees it as already cloned
        client._work_dir.mkdir(parents=True, exist_ok=True)

        proc = _make_proc()
        with patch("asyncio.create_subprocess_exec", return_value=proc) as mock_exec:
            await client.clone_or_fetch()

        calls = [call.args for call in mock_exec.call_args_list]
        assert any(call[1] == "fetch" for call in calls), (
            f"Expected a 'git fetch' call, got: {calls}"
        )
        assert not any(call[1] == "clone" for call in calls)

    @pytest.mark.asyncio
    async def test_clone_embeds_token_in_url(self, tmp_path: Path) -> None:
        cache_dir = str(tmp_path / "cache")
        client = GitClient(
            repo_url="https://example.com/repo.git",
            token="s3cr3t",
            cache_dir=cache_dir,
        )

        proc = _make_proc()
        with patch("asyncio.create_subprocess_exec", return_value=proc) as mock_exec:
            await client.clone_or_fetch()

        # Find the clone call and check the URL argument
        clone_call = next(
            c for c in mock_exec.call_args_list if c.args[1] == "clone"
        )
        cloned_url: str = clone_call.args[2]
        assert "s3cr3t" in cloned_url
        assert cloned_url.startswith("https://token:")


# ---------------------------------------------------------------------------
# resolve_ref
# ---------------------------------------------------------------------------

class TestResolveRef:
    @pytest.mark.asyncio
    async def test_returns_commit_hash(self, tmp_path: Path) -> None:
        cache_dir = str(tmp_path / "cache")
        client = GitClient(
            repo_url="https://example.com/repo.git",
            cache_dir=cache_dir,
        )
        client._work_dir.mkdir(parents=True, exist_ok=True)

        commit_hash = "abc123def456abc123def456abc123def456abc1"
        proc = _make_proc(stdout=(commit_hash + "\n").encode())
        with patch("asyncio.create_subprocess_exec", return_value=proc):
            result = await client.resolve_ref("main")

        assert result == commit_hash

    @pytest.mark.asyncio
    async def test_passes_correct_rev_parse_arg(self, tmp_path: Path) -> None:
        cache_dir = str(tmp_path / "cache")
        client = GitClient(
            repo_url="https://example.com/repo.git",
            cache_dir=cache_dir,
        )
        client._work_dir.mkdir(parents=True, exist_ok=True)

        proc = _make_proc(stdout=b"deadbeef\n")
        with patch("asyncio.create_subprocess_exec", return_value=proc) as mock_exec:
            await client.resolve_ref("v1.0.0")

        args = mock_exec.call_args.args
        # git rev-parse v1.0.0^{commit}
        assert args[1] == "rev-parse"
        assert "v1.0.0^{commit}" in args[2]


# ---------------------------------------------------------------------------
# read_file
# ---------------------------------------------------------------------------

class TestReadFile:
    @pytest.mark.asyncio
    async def test_reads_existing_file(self, tmp_path: Path) -> None:
        cache_dir = str(tmp_path / "cache")
        client = GitClient(
            repo_url="https://example.com/repo.git",
            cache_dir=cache_dir,
        )
        client._work_dir.mkdir(parents=True, exist_ok=True)
        (client._work_dir / "script.py").write_text("print('hello')", encoding="utf-8")

        content = await client.read_file("script.py")
        assert content == "print('hello')"

    @pytest.mark.asyncio
    async def test_path_traversal_raises_value_error(self, tmp_path: Path) -> None:
        cache_dir = str(tmp_path / "cache")
        client = GitClient(
            repo_url="https://example.com/repo.git",
            cache_dir=cache_dir,
        )
        client._work_dir.mkdir(parents=True, exist_ok=True)

        with pytest.raises(ValueError, match="Path traversal"):
            await client.read_file("../../etc/passwd")

    @pytest.mark.asyncio
    async def test_path_traversal_absolute_raises_value_error(self, tmp_path: Path) -> None:
        cache_dir = str(tmp_path / "cache")
        client = GitClient(
            repo_url="https://example.com/repo.git",
            cache_dir=cache_dir,
        )
        client._work_dir.mkdir(parents=True, exist_ok=True)

        with pytest.raises(ValueError, match="Path traversal"):
            await client.read_file("/etc/passwd")

    @pytest.mark.asyncio
    async def test_reads_nested_file(self, tmp_path: Path) -> None:
        cache_dir = str(tmp_path / "cache")
        client = GitClient(
            repo_url="https://example.com/repo.git",
            cache_dir=cache_dir,
        )
        subdir = client._work_dir / "scripts" / "sub"
        subdir.mkdir(parents=True, exist_ok=True)
        (subdir / "run.sh").write_text("#!/bin/bash\necho hi", encoding="utf-8")

        content = await client.read_file("scripts/sub/run.sh")
        assert "echo hi" in content


# ---------------------------------------------------------------------------
# get_file_at_ref
# ---------------------------------------------------------------------------

class TestGetFileAtRef:
    @pytest.mark.asyncio
    async def test_calls_checkout_then_read_file(self, tmp_path: Path) -> None:
        cache_dir = str(tmp_path / "cache")
        client = GitClient(
            repo_url="https://example.com/repo.git",
            cache_dir=cache_dir,
        )
        client._work_dir.mkdir(parents=True, exist_ok=True)
        (client._work_dir / "main.py").write_text("x = 1", encoding="utf-8")

        checked_out_refs: list[str] = []

        async def fake_checkout(ref: str) -> None:
            checked_out_refs.append(ref)

        client.checkout = fake_checkout  # type: ignore[method-assign]

        content = await client.get_file_at_ref("v2.0.0", "main.py")

        assert checked_out_refs == ["v2.0.0"]
        assert content == "x = 1"


# ---------------------------------------------------------------------------
# GitError on non-zero exit code
# ---------------------------------------------------------------------------

class TestGitError:
    @pytest.mark.asyncio
    async def test_non_zero_exit_raises_git_error(self, tmp_path: Path) -> None:
        cache_dir = str(tmp_path / "cache")
        client = GitClient(
            repo_url="https://example.com/repo.git",
            cache_dir=cache_dir,
        )
        client._work_dir.mkdir(parents=True, exist_ok=True)

        proc = _make_proc(
            returncode=128,
            stderr=b"fatal: not a git repository\n",
        )
        with patch("asyncio.create_subprocess_exec", return_value=proc):
            with pytest.raises(GitError) as exc_info:
                await client._run_git("status", cwd=client._work_dir)

        assert "128" in str(exc_info.value) or "failed" in str(exc_info.value).lower()
        assert "not a git repository" in exc_info.value.stderr

    @pytest.mark.asyncio
    async def test_git_error_carries_stderr(self, tmp_path: Path) -> None:
        err_msg = b"error: pathspec 'nonexistent' did not match\n"
        proc = _make_proc(returncode=1, stderr=err_msg)

        cache_dir = str(tmp_path / "cache")
        client = GitClient(
            repo_url="https://example.com/repo.git",
            cache_dir=cache_dir,
        )
        client._work_dir.mkdir(parents=True, exist_ok=True)

        with patch("asyncio.create_subprocess_exec", return_value=proc):
            with pytest.raises(GitError) as exc_info:
                await client.checkout("nonexistent")

        assert "pathspec" in exc_info.value.stderr
