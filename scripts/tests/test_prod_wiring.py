from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from check_prod_wiring import Paths, check  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "scripts" / "check_prod_wiring.py"


class ProdWiringTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name)
        real = Paths.under(ROOT)
        self.paths = Paths.under(self.root)
        for field in ("manifest", "dev_compose", "prod_compose", "deploy_workflow", "terraform_database"):
            target = getattr(self.paths, field)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(getattr(real, field), target)
        for migrations in real.backend_services.glob("*/migrations"):
            (self.paths.backend_services / migrations.parent.name / "migrations").mkdir(parents=True)

    def edit(self, path: Path, old: str, new: str) -> None:
        text = path.read_text(encoding="utf-8")
        self.assertEqual(text.count(old), 1, f"{old!r} must occur once in {path.name}")
        path.write_text(text.replace(old, new), encoding="utf-8")

    def assert_error(self, fragment: str) -> None:
        errors = check(self.paths)
        self.assertTrue(any(fragment in e for e in errors), f"{fragment!r} not in {errors}")

    def test_current_wiring_passes(self) -> None:
        self.assertEqual(check(Paths.under(ROOT)), [])

    def test_cli_runs_with_one_command(self) -> None:
        result = subprocess.run(
            [sys.executable, str(CHECKER)], cwd=ROOT, capture_output=True, text=True, check=False
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_cli_fails_and_lists_drift(self) -> None:
        self.edit(self.paths.deploy_workflow, "          - service: sensor-simulator\n", "")
        result = subprocess.run(
            [sys.executable, str(CHECKER), "--root", str(self.root)],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("sensor-simulator: image sensor-simulator is not in the deploy.yml", result.stderr)

    def test_missing_prod_service_fails(self) -> None:
        self.edit(self.paths.prod_compose, "\n  config-service:\n", "\n  config-service-renamed:\n")
        self.assert_error("config-service: missing from docker-compose.prod.yml")
        self.assert_error("config-service-renamed: in docker-compose.prod.yml but not in the manifest")

    def test_new_dev_service_must_be_declared(self) -> None:
        self.edit(
            self.paths.dev_compose,
            "\n  mailpit:\n",
            "\n  rabbitmq:\n    image: rabbitmq:4\n\n  mailpit:\n",
        )
        self.assert_error("rabbitmq: in docker-compose.yml but neither in the manifest")

    def test_prod_env_removed_fails(self) -> None:
        self.edit(
            self.paths.prod_compose,
            "      - CONFIG_CLIENT_URL=${CONFIG_CLIENT_URL:?CONFIG_CLIENT_URL must be set in production .env}\n",
            "",
        )
        self.assert_error("telemetry-ingest-service: env CONFIG_CLIENT_URL is set only in docker-compose.yml")

    def test_stale_allowlist_entry_fails(self) -> None:
        # RABBITMQ_URL is allowlisted as dev-only; once prod sets it too, the entry is stale.
        self.edit(
            self.paths.prod_compose,
            "      - S3_BUCKET=",
            "      - RABBITMQ_URL=amqp://rabbitmq:5672/\n      - S3_BUCKET=",
        )
        self.assert_error("env_differences.experiment-service.dev_only.RABBITMQ_URL is allowlisted")

    def test_build_matrix_entry_removed_fails(self) -> None:
        self.edit(self.paths.deploy_workflow, "          - service: script-service\n", "")
        self.assert_error("script-service: image script-service is not in the deploy.yml build matrix")

    def test_unreleased_matrix_image_fails(self) -> None:
        self.edit(
            self.paths.deploy_workflow,
            "          - service: sensor-simulator\n",
            "          - service: sensor-simulator\n          - service: orphan-service\n",
        )
        self.assert_error("deploy.yml builds orphan-service, but no manifest service uses it")

    def test_migrate_dependency_removed_fails(self) -> None:
        self.edit(
            self.paths.prod_compose,
            "      script-migrate:\n        condition: service_completed_successfully\n",
            "      script-migrate:\n        condition: service_started\n",
        )
        self.assert_error("script-service: in docker-compose.prod.yml does not wait for script-migrate")

    def test_migrate_mapping_removed_fails(self) -> None:
        self.edit(self.paths.manifest, "    migrate: config-migrate\n", "")
        self.assert_error("config-service: has migrations/ but no migrate service in the manifest")

    def test_terraform_database_removed_fails(self) -> None:
        self.edit(self.paths.terraform_database, 'name       = "script_db"', 'name       = "scripts_db"')
        self.assert_error("script-service: database script_db is not created in Terraform")

    def test_dev_only_service_in_prod_fails(self) -> None:
        self.edit(
            self.paths.prod_compose,
            "\n  loki:\n",
            "\n  mailpit:\n    image: axllent/mailpit\n\n  loki:\n",
        )
        self.assert_error("mailpit: listed in dev_only_services but present in docker-compose.prod.yml")


if __name__ == "__main__":
    unittest.main()
