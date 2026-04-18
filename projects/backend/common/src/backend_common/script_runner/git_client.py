"""Async git client for fetching scripts from remote repositories."""
from __future__ import annotations

import asyncio
import hashlib
from pathlib import Path
from urllib.parse import quote, urlparse, urlunparse

import structlog

logger = structlog.get_logger(__name__)


class GitError(Exception):
    def __init__(self, message: str, stderr: str = "") -> None:
        super().__init__(message)
        self.stderr = stderr


class GitClient:
    def __init__(
        self,
        repo_url: str,
        token: str | None = None,
        cache_dir: str = "/tmp/script_runner_repos",
        timeout_sec: int = 60,
    ) -> None:
        self._repo_url = repo_url
        self._token = token
        self._timeout_sec = timeout_sec
        self._lock = asyncio.Lock()

        cache_key = hashlib.sha256(repo_url.encode()).hexdigest()[:16]
        self._work_dir = Path(cache_dir) / cache_key
        Path(cache_dir).mkdir(parents=True, exist_ok=True)

    def _authenticated_url(self) -> str:
        """Return repo URL with token embedded, or plain URL if no token."""
        if not self._token:
            return self._repo_url

        parsed = urlparse(self._repo_url)
        quoted_token = quote(self._token, safe="")
        netloc = f"token:{quoted_token}@{parsed.hostname}"
        if parsed.port:
            netloc = f"{netloc}:{parsed.port}"
        return urlunparse(parsed._replace(netloc=netloc))

    async def _run_git(self, *args: str, cwd: Path | None = None) -> tuple[str, str]:
        """Run a git command, return (stdout, stderr). Raise GitError on non-zero exit."""
        cmd = ("git", *args)
        proc = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
            cwd=str(cwd) if cwd is not None else None,
        )
        try:
            stdout_bytes, stderr_bytes = await asyncio.wait_for(
                proc.communicate(),
                timeout=self._timeout_sec,
            )
        except asyncio.TimeoutError:
            try:
                proc.kill()
            except ProcessLookupError:
                pass
            await proc.wait()
            raise GitError(
                f"git {args[0]} timed out after {self._timeout_sec}s",
                stderr="timeout",
            )

        stdout = stdout_bytes.decode(errors="replace")
        stderr = stderr_bytes.decode(errors="replace")

        if proc.returncode != 0:
            raise GitError(
                f"git {args[0]} failed with exit code {proc.returncode}",
                stderr=stderr,
            )

        return stdout, stderr

    async def clone_or_fetch(self) -> None:
        """Clone repo if not present locally, otherwise fetch all refs."""
        async with self._lock:
            if self._work_dir.exists():
                logger.info(
                    "git_fetch",
                    repo_url=self._repo_url,
                    work_dir=str(self._work_dir),
                )
                await self._run_git(
                    "fetch", "--all", "--prune", "--tags",
                    cwd=self._work_dir,
                )
            else:
                logger.info(
                    "git_clone",
                    repo_url=self._repo_url,
                    work_dir=str(self._work_dir),
                )
                auth_url = self._authenticated_url()
                await self._run_git("clone", auth_url, str(self._work_dir))

    async def checkout(self, ref: str) -> None:
        """Checkout ref in detached HEAD mode. Supports commit hash, tag, branch."""
        async with self._lock:
            logger.info(
                "git_checkout",
                repo_url=self._repo_url,
                ref=ref,
            )
            await self._run_git("checkout", "--detach", ref, cwd=self._work_dir)

    async def resolve_ref(self, ref: str) -> str:
        """Return the commit hash that ref points to."""
        async with self._lock:
            stdout, _ = await self._run_git(
                "rev-parse", f"{ref}^{{commit}}",
                cwd=self._work_dir,
            )
            return stdout.strip()

    async def read_file(self, path: str) -> str:
        """Read a file from the working copy.

        Raises ValueError if the resolved path escapes the working directory
        (path traversal protection).
        """
        work_dir_real = self._work_dir.resolve()
        target = (self._work_dir / path).resolve()

        if not str(target).startswith(str(work_dir_real) + "/") and target != work_dir_real:
            raise ValueError(
                f"Path traversal detected: {path!r} resolves outside the repository"
            )

        return target.read_text(encoding="utf-8")

    async def get_file_at_ref(self, ref: str, path: str) -> str:
        """Checkout ref then return the contents of path."""
        await self.checkout(ref)
        return await self.read_file(path)
