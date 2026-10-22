"""Exercise the real Make entry points without Docker, Poetry, or network access."""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from urllib.parse import quote

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ["dev-common.sh", "backend.sh", "frontend.sh", "dev.sh", "show-help.sh", "firmware-coverage.sh", "pytest_totals.py"]

# External tools record their arguments and emulate only the boundary behavior.
FAKE_TOOL = r'''
import json, os, pathlib, sys
name = pathlib.Path(sys.argv[0]).name
args = sys.argv[1:]
with open(os.environ["COMMAND_LOG"], "a") as stream:
    stream.write(json.dumps({"tool": name, "args": args, "cwd": os.getcwd(),
                             "dsn": os.environ.get("TEST_POSTGRESQL_DSN"),
                             "buildkit": os.environ.get("DOCKER_BUILDKIT")}) + "\n")
if name.startswith("python"):
    if args[:1] == ["-c"] and "sys.version_info" in args[1]:
        sys.exit(int(os.environ.get("OLD_PYTHON", "0")))
    if args[:2] == ["-m", "venv"]:
        destination = pathlib.Path(args[2]) / "bin" / "python"
        destination.parent.mkdir(parents=True)
        destination.write_text(pathlib.Path(sys.argv[0]).read_text())
        destination.chmod(0o755)
        sys.exit(0)
    if args[:2] == ["-m", "pip"]:
        sys.exit(0)
    os.execv(os.environ["REAL_PYTHON"], [os.environ["REAL_PYTHON"], *args])
if name == "poetry":
    if args[:2] == ["run", "pytest"]:
        print("2 passed in 0.01s")
        sys.exit(1 if pathlib.Path.cwd().name == os.environ.get("FAIL_SERVICE") else 0)
    sys.exit(1 if args[:1] == ["install"] and os.environ.get("FAIL_INSTALL") else 0)
if name == "docker":
    if args[:2] == ["buildx", "version"]:
        sys.exit(int(os.environ.get("NO_BUILDX", "0")))
    if args[:2] == ["image", "inspect"]:
        print(os.environ.get("IMAGE_HASH", "old"))
        sys.exit(0)
    if args[:1] == ["compose"]:
        args = args[1:]
    if "pg_isready" in args:
        sys.exit(int(os.environ.get("PG_NOT_READY", "0")))
    if args[:1] == ["port"]:
        print(os.environ.get("PG_PORT", "[::]:6543"))
    if "sh" in args and "POSTGRES_USER" in args[-1]:
        print(os.environ.get("PG_USER", "postgres"), end="")
    if "sh" in args and "POSTGRES_PASSWORD" in args[-1]:
        print(os.environ.get("PG_PASSWORD", "local-secret"), end="")
    if "psql" in args:
        sys.stdin.read()
    failure = os.environ.get("FAIL_DOCKER")
    sys.exit(1 if failure and failure in args else 0)
if name == "node":
    sys.exit(int(os.environ.get("OLD_NODE", "0")))
if name == "npm":
    sys.exit(int(os.environ.get("FAIL_NPM", "0")))
if name == "git":
    print(os.environ.get("GIT_STATUS", ""))
if name == "curl" and "-w" in args:
    print("201", end="")
'''


class MakeCommandsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="los85-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "scripts").mkdir()
        for name in SCRIPTS:
            shutil.copy(ROOT / "scripts" / name, self.root / "scripts" / name)
        shutil.copy(ROOT / "Makefile", self.root / "Makefile")
        for path in ["common", "first", "second", "infrastructure/logging"]:
            (self.root / path).mkdir(parents=True, exist_ok=True)
        (self.root / "env.docker.example").touch()
        (self.root / "docker-compose.override.yml.example").touch()
        self.bin = self.root / "bin"
        self.bin.mkdir()
        for name in ["python3.14", "python custom", "poetry", "docker", "node", "npm", "sleep", "git", "curl"]:
            script = self.bin / name
            script.write_text(f"#!{sys.executable}\n" + FAKE_TOOL)
            script.chmod(0o755)
        self.log = self.root / "commands.jsonl"
        self.env = {**os.environ, "PATH": f"{self.bin}:{os.environ['PATH']}",
                    "COMMAND_LOG": str(self.log), "REAL_PYTHON": sys.executable, "TMPDIR": str(self.root)}

    def run_make(self, *targets, **env):
        return subprocess.run(
            ["make", "--no-print-directory", *targets,
             "BACKEND_SERVICES=first second", "BACKEND_PROJECTS=common first second",
             "BACKEND_COMMON_DIR=common", "DOCKER_COMPOSE=docker compose",
             "BACKEND_BASE_HASH=current", "BACKEND_BASE_DOCKERFILE=base.Dockerfile"],
            cwd=self.root, env={**self.env, **env}, text=True, capture_output=True,
        )

    def calls(self, tool=None):
        calls = [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []
        return [call for call in calls if tool is None or call["tool"] == tool]

    def test_help_uses_target_annotations_without_installing_dependencies(self):
        with (self.root / "Makefile").open("a") as stream:
            stream.write("\ncustom-help-example: ## A newly documented target\n")
        result = self.run_make("help")
        self.assertEqual(result.returncode, 0, result.stderr)
        for text in ["dev-%", "test-backend", "A newly documented target"]:
            self.assertIn(text, result.stdout)
        self.assertEqual(self.calls(), [])

    def test_explicit_python_path_with_spaces_reaches_every_project(self):
        result = self.run_make("backend-install", PYTHON=str(self.bin / "python custom"))
        self.assertEqual(result.returncode, 0, result.stderr)
        uses = [call for call in self.calls("poetry") if call["args"][:2] == ["env", "use"]]
        self.assertEqual(len(uses), 3)
        self.assertTrue(all(call["args"][2] == str(self.bin / "python custom") for call in uses))

    def test_missing_preferred_python_uses_shared_fallback(self):
        result = self.run_make("backend-install", PYTHON="missing-python")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(self.calls("python3.14"))

    def test_old_python_and_failed_install_stop_before_tests(self):
        for env in [{"OLD_PYTHON": "1"}, {"FAIL_INSTALL": "1"}]:
            with self.subTest(env=env):
                self.log.unlink(missing_ok=True)
                result = self.run_make("test-backend", **env)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(any(call["args"][:2] == ["run", "pytest"] for call in self.calls("poetry")))
                self.assertFalse(self.calls("docker"))

    def test_supplied_dsn_skips_docker_and_failure_is_aggregated(self):
        dsn = "postgresql://user:do-not-print@db:5432/tests"
        result = self.run_make("test-backend", TEST_POSTGRESQL_DSN=dsn, FAIL_SERVICE="first")
        self.assertNotEqual(result.returncode, 0)
        tests = [call for call in self.calls("poetry") if call["args"][:2] == ["run", "pytest"]]
        self.assertEqual([Path(call["cwd"]).name for call in tests], ["common", "first", "second"])
        self.assertEqual(tests[0]["dsn"], dsn)
        self.assertTrue(all(call["args"][-2:] == ["--postgresql", dsn] for call in tests[1:]))
        self.assertFalse(self.calls("docker"))
        self.assertNotIn("do-not-print", result.stdout + result.stderr)

    def test_test_and_coverage_share_readiness_and_credential_discovery(self):
        for target in ["test-backend", "test-coverage-backend"]:
            with self.subTest(target=target):
                self.log.unlink(missing_ok=True)
                result = self.run_make(target, PG_USER="name@host", PG_PASSWORD="p:a'ss/?")
                self.assertEqual(result.returncode, 0, result.stderr)
                tests = [call for call in self.calls("poetry") if "--postgresql" in call["args"]]
                expected = f"postgresql://{quote('name@host', safe='')}:{quote('p:a' + chr(39) + 'ss/?', safe='')}@localhost:6543/postgres"
                self.assertTrue(all(call["args"][call["args"].index("--postgresql") + 1] == expected for call in tests))
                self.assertEqual(len(tests), 2)
                self.assertTrue(any("pg_isready" in call["args"] for call in self.calls("docker")))
                self.assertNotIn("p:a'ss/?", result.stdout + result.stderr)
                if target.endswith("coverage-backend"):
                    self.assertTrue(all("--cov-report=xml" in call["args"] for call in tests))

    def test_failed_database_start_readiness_or_port_prevents_pytest(self):
        for target in ["test-backend", "test-coverage-backend"]:
            for env in [{"FAIL_DOCKER": "up"}, {"PG_NOT_READY": "1"}, {"PG_PORT": "invalid"}]:
                with self.subTest(target=target, env=env):
                    self.log.unlink(missing_ok=True)
                    result = self.run_make(target, **env)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertFalse(any(call["args"][:2] == ["run", "pytest"] for call in self.calls("poetry")))

    def test_cleanup_is_limited_to_compose_projects(self):
        for target in ["dev-clean", "dev-clean-all"]:
            with self.subTest(target=target):
                self.log.unlink(missing_ok=True)
                result = self.run_make(target)
                self.assertEqual(result.returncode, 0, result.stderr)
                commands = self.calls("docker")
                self.assertEqual(len(commands), 2)
                self.assertTrue(all("down" in call["args"] and "-v" in call["args"] and "--remove-orphans" in call["args"] for call in commands))
                self.assertEqual(Path(commands[1]["cwd"]).relative_to(self.root).as_posix(), "infrastructure/logging")
                self.assertEqual("--rmi" in commands[0]["args"], target == "dev-clean-all")

    def test_cleanup_error_is_propagated(self):
        result = self.run_make("dev-clean", FAIL_DOCKER="down")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(len(self.calls("docker")), 1)

    def test_dev_fix_retains_volumes_and_seeds_after_restart(self):
        result = self.run_make("dev-fix", IMAGE_HASH="current")
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = self.calls("docker")
        self.assertEqual(commands[0]["args"], ["compose", "down", "--remove-orphans"])
        self.assertTrue(any("up" in call["args"] for call in commands))
        self.assertTrue(self.calls("curl"))
        self.assertTrue((self.root / ".env").exists())

    def test_buildkit_fallback_and_forced_base_rebuild(self):
        result = self.run_make("backend-base-no-cache", IMAGE_HASH="current", NO_BUILDX="1")
        self.assertEqual(result.returncode, 0, result.stderr)
        builds = [call for call in self.calls("docker") if call["args"][:1] == ["build"]]
        self.assertEqual(len(builds), 1)
        self.assertIn("--no-cache", builds[0]["args"])
        self.assertEqual(builds[0]["buildkit"], "0")

    def test_unknown_backend_service_is_rejected_before_docker_build(self):
        result = self.run_make("dev-unknown")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.calls("docker"))

    def test_frontend_local_and_docker_errors_are_not_hidden(self):
        for env in [{"FAIL_NPM": "1"}, {"OLD_NODE": "1", "FAIL_DOCKER": "run"}]:
            with self.subTest(env=env):
                result = self.run_make("test-frontend", **env)
                self.assertNotEqual(result.returncode, 0)

    def test_telemetry_cli_uses_shared_python_and_creates_venv_once(self):
        (self.root / "projects/telemetry_cli/tests").mkdir(parents=True)
        (self.root / "projects/telemetry_cli/src").mkdir()
        (self.root / "projects/telemetry_cli/tests/test_example.py").write_text(
            "import unittest\nclass Example(unittest.TestCase):\n    def test_example(self): self.assertTrue(True)\n"
        )
        for _ in range(2):
            result = self.run_make("test-telemetry-cli", PYTHON=str(self.bin / "python custom"))
            self.assertEqual(result.returncode, 0, result.stderr)
        creations = [call for call in self.calls("python custom") if call["args"][:2] == ["-m", "venv"]]
        self.assertEqual(len(creations), 1)
        self.assertTrue(any(call["args"][:2] == ["-m", "unittest"] for call in self.calls("python")))

    def test_changed_frontend_files_rebuild_only_the_affected_service(self):
        result = self.run_make("dev-rebuild-changed", GIT_STATUS=" M projects/frontend/apps/auth-proxy/src/server.ts")
        self.assertEqual(result.returncode, 0, result.stderr)
        builds = [call for call in self.calls("docker") if call["args"][:2] == ["compose", "build"]]
        self.assertEqual([call["args"] for call in builds], [["compose", "build", "auth-proxy"]])

    def test_shell_syntax(self):
        for name in SCRIPTS:
            if name.endswith(".sh"):
                result = subprocess.run(["bash", "-n", str(ROOT / "scripts" / name)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
