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
PRODUCTION_CONTRACT_WORKFLOW = ROOT / ".github" / "workflows" / "production-contract-tests.yml"
TERRAFORM_OUTPUTS = ROOT / "infrastructure" / "yandex-cloud" / "outputs.tf"
TERRAFORM_OBJECT_STORAGE = ROOT / "infrastructure" / "yandex-cloud" / "object-storage.tf"

EXPECTED_REQUIRED_KEYS = {
    "AUTH_DATABASE_URL",
    "CONFIG_DATABASE_URL",
    "CONFIG_CLIENT_ENABLED",
    "CONFIG_CLIENT_POLL_INTERVAL_SECONDS",
    "CONFIG_CLIENT_URL",
    "COOKIE_DOMAIN",
    "CORS_ALLOWED_ORIGINS",
    "CORS_ORIGINS",
    "CR_REGISTRY",
    "EXPERIMENT_DATABASE_URL",
    "GRAFANA_ADMIN_PASSWORD",
    "JWT_SECRET",
    "REDIS_URL",
    "S3_ACCESS_KEY",
    "S3_BUCKET",
    "S3_ENDPOINT_URL",
    "S3_PRESIGN_EXPIRE_SECONDS",
    "S3_PUBLIC_ENDPOINT_URL",
    "S3_SECRET_KEY",
    "SCRIPT_DATABASE_URL",
    "TELEMETRY_BROKER_URL",
}

VALID_ENV = """\
CR_REGISTRY=cr.yandex/test-registry
AUTH_DATABASE_URL=postgresql://auth_user:secret@db.example.net:6432/auth_db?sslmode=verify-full
EXPERIMENT_DATABASE_URL=postgresql://experiment_user:secret@db.example.net:6432/experiment_db?sslmode=verify-full
CONFIG_DATABASE_URL=postgresql://config_user:secret@db.example.net:6432/config_db?sslmode=verify-full
SCRIPT_DATABASE_URL=postgresql://script_user:secret@db.example.net:6432/script_db?sslmode=verify-full
JWT_SECRET=a-real-secret-longer-than-thirty-two-characters
COOKIE_DOMAIN=prod.example.net
CORS_ORIGINS=https://prod.example.net
CORS_ALLOWED_ORIGINS=https://prod.example.net
GRAFANA_ADMIN_PASSWORD=a-real-grafana-secret
REDIS_URL=redis://redis:6379/0
TELEMETRY_BROKER_URL=redis://redis:6379/0
CONFIG_CLIENT_ENABLED=true
CONFIG_CLIENT_URL=http://config-service:8005
CONFIG_CLIENT_POLL_INTERVAL_SECONDS=5.0
S3_ENDPOINT_URL=https://storage.yandexcloud.net
S3_PUBLIC_ENDPOINT_URL=https://storage.yandexcloud.net
S3_ACCESS_KEY=test-access-key
S3_SECRET_KEY=test-secret-key
S3_BUCKET=test-artifacts-bucket
S3_PRESIGN_EXPIRE_SECONDS=3600
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

    def test_redis_is_internal_persistent_and_health_checked(self) -> None:
        compose = COMPOSE.read_text(encoding="utf-8")
        redis_service = re.search(r"^  redis:\n(?P<body>.*?)(?=^  [a-zA-Z0-9_-]+:\n)", compose, re.MULTILINE | re.DOTALL)
        self.assertIsNotNone(redis_service)
        body = redis_service.group("body")
        self.assertIn("image: redis:7.4.9-alpine", body)
        self.assertIn("redis_data:/data", body)
        self.assertNotIn("ports:", body)
        self.assertIn('["CMD", "redis-cli", "ping"]', body)
        self.assertIn("restart: unless-stopped", body)
        self.assertRegex(compose, r"(?m)^  redis_data:\n    name: experiment-redis-data$")

    def test_redis_consumers_wait_for_healthy_redis(self) -> None:
        compose = COMPOSE.read_text(encoding="utf-8")
        self.assertIn("TELEMETRY_BROKER_URL=${TELEMETRY_BROKER_URL:?", compose)
        self.assertIn("REDIS_URL=${REDIS_URL:?", compose)
        self.assertEqual(compose.count("redis:\n        condition: service_healthy"), 2)

    def test_telemetry_config_client_contract_is_explicit(self) -> None:
        compose = COMPOSE.read_text(encoding="utf-8")
        for key in ("CONFIG_CLIENT_ENABLED", "CONFIG_CLIENT_URL", "CONFIG_CLIENT_POLL_INTERVAL_SECONDS"):
            self.assertIn(f"{key}=${{{key}:?", compose)
        self.assertIn("config-service:\n        condition: service_healthy", compose)

    def test_script_service_production_wiring_is_explicit(self) -> None:
        compose = COMPOSE.read_text(encoding="utf-8")
        self.assertIn("DATABASE_URL=${SCRIPT_DATABASE_URL:?", compose)
        self.assertIn("script-migrate:\n        condition: service_completed_successfully", compose)
        self.assertIn("TARGET_SCRIPT_URL=http://script-service:8004", compose)
        self.assertIn("script-service:\n        condition: service_healthy", compose)

    def test_object_storage_runtime_contract_is_explicit(self) -> None:
        compose = COMPOSE.read_text(encoding="utf-8")
        for key in (
            "S3_ENDPOINT_URL",
            "S3_PUBLIC_ENDPOINT_URL",
            "S3_ACCESS_KEY",
            "S3_SECRET_KEY",
            "S3_BUCKET",
            "S3_PRESIGN_EXPIRE_SECONDS",
        ):
            self.assertIn(f"{key}=${{{key}:?", compose)

    def test_artifact_storage_iam_is_bucket_scoped(self) -> None:
        storage = TERRAFORM_OBJECT_STORAGE.read_text(encoding="utf-8")
        self.assertIn('resource "yandex_iam_service_account_static_access_key" "artifacts_sa_key"', storage)
        self.assertIn('resource "yandex_storage_bucket" "artifacts"', storage)
        self.assertIn('resource "yandex_storage_bucket_iam_binding" "artifacts_editor"', storage)
        self.assertIn('role   = "storage.editor"', storage)
        self.assertIn('bucket = yandex_storage_bucket.artifacts.bucket', storage)
        self.assertNotIn('yandex_resourcemanager_folder_iam_member', storage)
        self.assertIn('force_destroy = false', storage)

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

    def test_s3_template_values_fail(self) -> None:
        placeholders = {
            "S3_ACCESS_KEY": "CHANGE_ME_TERRAFORM_OUTPUT_ARTIFACTS_S3_ACCESS_KEY",
            "S3_SECRET_KEY": "CHANGE_ME_TERRAFORM_OUTPUT_ARTIFACTS_S3_SECRET_KEY",
            "S3_BUCKET": "YOUR_ARTIFACTS_BUCKET_NAME",
        }
        for key, placeholder in placeholders.items():
            with self.subTest(key=key):
                result = self.run_validator(
                    re.sub(rf"^{key}=.*$", f"{key}={placeholder}", VALID_ENV, flags=re.MULTILINE)
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(f"{key} still contains a template value", result.stderr)

    def test_release_workflow_validates_before_stopping_stack(self) -> None:
        workflow = DEPLOY_WORKFLOW.read_text(encoding="utf-8")
        validation = workflow.index("./validate-production-env.sh .env docker-compose.prod.yml")
        first_down = workflow.index("docker compose -p experiment-tracking")
        self.assertLess(validation, first_down)

    def test_release_contract_job_declares_every_required_env(self) -> None:
        workflow = DEPLOY_WORKFLOW.read_text(encoding="utf-8")
        job = re.search(
            r"(?ms)^  production-contract-tests:\n(?P<body>.*?)(?=^  [a-zA-Z0-9_-]+:\n)",
            workflow,
        )
        self.assertIsNotNone(job)
        declared = set(re.findall(r"(?m)^      ([A-Z_][A-Z0-9_]*):", job.group("body")))
        self.assertEqual(EXPECTED_REQUIRED_KEYS - declared, set())

    def test_production_contract_workflow_declares_every_required_env(self) -> None:
        workflow = PRODUCTION_CONTRACT_WORKFLOW.read_text(encoding="utf-8")
        job = re.search(
            r"(?ms)^  production-contract:\n(?P<body>.*?)(?=^  [a-zA-Z0-9_-]+:\n|\Z)",
            workflow,
        )
        self.assertIsNotNone(job)
        declared = set(re.findall(r"(?m)^      ([A-Z_][A-Z0-9_]*):", job.group("body")))
        self.assertEqual(EXPECTED_REQUIRED_KEYS - declared, set())

    def test_terraform_database_url_outputs_do_not_contain_masked_passwords(self) -> None:
        outputs = TERRAFORM_OUTPUTS.read_text(encoding="utf-8")
        self.assertNotIn(":***@", outputs)
        self.assertIn("urlencode(var.pg_auth_db_password)", outputs)
        self.assertIn("urlencode(var.pg_experiment_db_password)", outputs)
        self.assertIn("urlencode(var.pg_config_db_password)", outputs)
        self.assertIn("urlencode(var.pg_script_db_password)", outputs)

    def test_object_storage_credentials_are_sensitive_outputs(self) -> None:
        outputs = TERRAFORM_OUTPUTS.read_text(encoding="utf-8")
        for output_name in ("artifacts_s3_access_key", "artifacts_s3_secret_key"):
            block = re.search(
                rf'output "{output_name}" \{{(?P<body>.*?)\n\}}', outputs, re.DOTALL
            )
            self.assertIsNotNone(block)
            self.assertIn("sensitive   = true", block.group("body"))


if __name__ == "__main__":
    unittest.main()
