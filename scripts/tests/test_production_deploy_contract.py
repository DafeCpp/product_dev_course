from __future__ import annotations

import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VALIDATOR = ROOT / "scripts" / "validate-production-env.sh"
COMPOSE = ROOT / "docker-compose.prod.yml"
ENV_EXAMPLE = ROOT / "env.production.example"
DEPLOY_WORKFLOW = ROOT / ".github" / "workflows" / "deploy.yml"
TERRAFORM_OUTPUTS = ROOT / "infrastructure" / "yandex-cloud" / "outputs.tf"

EXPECTED_REQUIRED_KEYS = {
    "AUTH_DATABASE_URL",
    "CONFIG_DATABASE_URL",
    "COOKIE_DOMAIN",
    "CORS_ALLOWED_ORIGINS",
    "CORS_ORIGINS",
    "CR_REGISTRY",
    "EXPERIMENT_DATABASE_URL",
    "GRAFANA_ADMIN_PASSWORD",
    "JWT_SECRET",
}

VALID_ENV = """\
CR_REGISTRY=cr.yandex/test-registry
AUTH_DATABASE_URL=postgresql://auth_user:secret@db.example.net:6432/auth_db?sslmode=verify-full
EXPERIMENT_DATABASE_URL=postgresql://experiment_user:secret@db.example.net:6432/experiment_db?sslmode=verify-full
CONFIG_DATABASE_URL=postgresql://config_user:secret@db.example.net:6432/config_db?sslmode=verify-full
JWT_SECRET=a-real-secret-longer-than-thirty-two-characters
COOKIE_DOMAIN=prod.example.net
CORS_ORIGINS=https://prod.example.net
CORS_ALLOWED_ORIGINS=https://prod.example.net
GRAFANA_ADMIN_PASSWORD=a-real-grafana-secret
"""


class ProductionDeployContractTest(unittest.TestCase):
    def run_validator(self, env_contents: str) -> subprocess.CompletedProcess[str]:
        with tempfile.NamedTemporaryFile("w", encoding="utf-8") as env_file:
            env_file.write(env_contents)
            env_file.flush()
            return subprocess.run(
                ["bash", str(VALIDATOR), env_file.name, str(COMPOSE)],
                cwd=ROOT,
                check=False,
                capture_output=True,
                text=True,
            )

    def test_required_keys_are_an_explicit_versioned_contract(self) -> None:
        compose = COMPOSE.read_text(encoding="utf-8")
        expressions = re.findall(r"\$\{([A-Z_][A-Z0-9_]*)(?::([^}]*))?\}", compose)
        required = {key for key, modifier in expressions if not modifier.startswith("-")}
        self.assertEqual(required, EXPECTED_REQUIRED_KEYS)

    def test_env_example_declares_every_required_key(self) -> None:
        example = ENV_EXAMPLE.read_text(encoding="utf-8")
        declared = {match.group(1) for match in re.finditer(r"^([A-Z_][A-Z0-9_]*)=", example, re.MULTILINE)}
        self.assertEqual(EXPECTED_REQUIRED_KEYS - declared, set())

    def test_valid_env_passes(self) -> None:
        result = self.run_validator(VALID_ENV)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("pre-flight passed", result.stdout)

    def test_missing_required_value_fails_without_leaking_other_secrets(self) -> None:
        secret = "do-not-print-this-jwt-secret"
        env_contents = VALID_ENV.replace(
            "JWT_SECRET=a-real-secret-longer-than-thirty-two-characters",
            f"JWT_SECRET={secret}",
        ).replace(
            "CONFIG_DATABASE_URL=postgresql://config_user:secret@db.example.net:6432/config_db?sslmode=verify-full\n",
            "",
        )

        result = self.run_validator(env_contents)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("CONFIG_DATABASE_URL is missing", result.stderr)
        self.assertNotIn(secret, result.stdout + result.stderr)
        self.assertIn("running stack was not changed", result.stderr)

    def test_empty_required_value_fails(self) -> None:
        result = self.run_validator(
            VALID_ENV.replace("GRAFANA_ADMIN_PASSWORD=a-real-grafana-secret", "GRAFANA_ADMIN_PASSWORD=   ")
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("GRAFANA_ADMIN_PASSWORD is empty", result.stderr)

    def test_template_value_fails(self) -> None:
        result = self.run_validator(
            VALID_ENV.replace("CR_REGISTRY=cr.yandex/test-registry", "CR_REGISTRY=cr.yandex/YOUR_REGISTRY_ID")
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("CR_REGISTRY still contains a template value", result.stderr)

    def test_release_workflow_validates_before_stopping_stack(self) -> None:
        workflow = DEPLOY_WORKFLOW.read_text(encoding="utf-8")
        validation = workflow.index("./validate-production-env.sh .env docker-compose.prod.yml")
        first_down = workflow.index("docker compose -p experiment-tracking")
        self.assertLess(validation, first_down)

    def test_terraform_database_url_outputs_do_not_contain_masked_passwords(self) -> None:
        outputs = TERRAFORM_OUTPUTS.read_text(encoding="utf-8")
        self.assertNotIn(":***@", outputs)
        self.assertIn("urlencode(var.pg_auth_db_password)", outputs)
        self.assertIn("urlencode(var.pg_experiment_db_password)", outputs)
        self.assertIn("urlencode(var.pg_config_db_password)", outputs)


if __name__ == "__main__":
    unittest.main()
