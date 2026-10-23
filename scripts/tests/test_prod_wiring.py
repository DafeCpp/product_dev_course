from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import yaml

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

    def set_env(self, path: Path, service: str, key: str, value: str) -> None:
        compose = yaml.safe_load(path.read_text(encoding="utf-8"))
        env = compose["services"][service]["environment"]
        index = next(i for i, item in enumerate(env) if item.startswith(f"{key}="))
        env[index] = f"{key}={value}"
        path.write_text(yaml.safe_dump(compose, sort_keys=False), encoding="utf-8")

    def test_service_pointed_at_another_database_fails(self) -> None:
        self.set_env(
            self.paths.prod_compose,
            "script-migrate",
            "DATABASE_URL",
            "${AUTH_DATABASE_URL:?AUTH_DATABASE_URL must be set in production .env}",
        )
        self.assert_error("script-migrate: in docker-compose.prod.yml DATABASE_URL must be ${SCRIPT_DATABASE_URL:?...}")

    def test_dev_default_dsn_for_another_database_fails(self) -> None:
        self.set_env(
            self.paths.dev_compose,
            "config-service",
            "DATABASE_URL",
            "${CONFIG_DATABASE_URL:-postgresql://config_user:config_password@postgres:5432/auth_db}",
        )
        self.assert_error("config-service: in docker-compose.yml the default DATABASE_URL does not point at config_db")

    def set_service_field(self, path: Path, service: str, *keys: str, value: object) -> None:
        compose = yaml.safe_load(path.read_text(encoding="utf-8"))
        node = compose["services"][service]
        for key in keys[:-1]:
            node = node[key]
        node[keys[-1]] = value
        path.write_text(yaml.safe_dump(compose, sort_keys=False), encoding="utf-8")

    def test_matrix_entry_building_another_dockerfile_fails(self) -> None:
        self.edit(
            self.paths.deploy_workflow,
            "            dockerfile: services/script-service/Dockerfile",
            "            dockerfile: services/config-service/Dockerfile",
        )
        self.assert_error(
            "script-service: deploy.yml matrix builds services/config-service/Dockerfile"
        )

    def test_dev_migrate_building_another_service_fails(self) -> None:
        self.set_service_field(
            self.paths.dev_compose,
            "script-migrate",
            "build",
            "dockerfile",
            value="services/config-service/Dockerfile",
        )
        self.assert_error("script-migrate: docker-compose.yml builds services/config-service/Dockerfile")

    def test_dev_runtime_building_another_service_fails(self) -> None:
        self.set_service_field(
            self.paths.dev_compose, "auth-service", "build", "context", value="./projects/other"
        )
        self.assert_error("auth-service: docker-compose.yml builds services/auth-service/Dockerfile from ./projects/other")

    def test_prod_migrate_with_another_image_fails(self) -> None:
        self.set_service_field(
            self.paths.prod_compose,
            "script-migrate",
            "image",
            value="${CR_REGISTRY:?CR_REGISTRY must be set in production .env}/config-service:${IMAGE_TAG:-latest}",
        )
        self.assert_error("script-migrate: docker-compose.prod.yml image")

    def test_extra_registry_path_in_prod_image_fails(self) -> None:
        # The same wrong path on a runtime and its migrate job used to pass.
        for service in ("auth-service", "auth-migrate"):
            self.set_service_field(
                self.paths.prod_compose,
                service,
                "image",
                value="${CR_REGISTRY:?CR_REGISTRY must be set in production .env}/extra/auth-service:${IMAGE_TAG:-latest}",
            )
        self.assert_error("auth-service: prod image")

    def test_duplicate_matrix_entry_fails(self) -> None:
        self.edit(
            self.paths.deploy_workflow,
            "          - service: sensor-simulator\n",
            "          - service: sensor-simulator\n            context: .\n"
            "            dockerfile: wrong/Dockerfile\n          - service: sensor-simulator\n",
        )
        self.assert_error("deploy.yml build matrix lists sensor-simulator more than once")

    def test_migrate_job_running_the_app_fails(self) -> None:
        self.set_service_field(
            self.paths.prod_compose,
            "auth-migrate",
            "command",
            value=["python", "-m", "auth_service.main"],
        )
        self.assert_error("auth-migrate: in docker-compose.prod.yml command must be")

    def test_migrate_job_restarting_fails(self) -> None:
        self.set_service_field(self.paths.dev_compose, "auth-migrate", "restart", value="unless-stopped")
        self.assert_error("auth-migrate: in docker-compose.yml restart must be 'no'")

    def test_shared_database_migration_order_is_required(self) -> None:
        self.set_service_field(
            self.paths.prod_compose,
            "telemetry-ingest-migrate",
            "depends_on",
            value={},
        )
        self.assert_error(
            "telemetry-ingest-migrate: in docker-compose.prod.yml does not wait for experiment-migrate"
        )

    def test_migrate_service_without_database_fails(self) -> None:
        self.edit(self.paths.manifest, "    database: auth_db\n", "")
        self.assert_error("auth-service: has a migrate job, so the manifest must declare its database")

    def test_migrate_after_must_be_a_migrate_job(self) -> None:
        self.edit(self.paths.manifest, "migrate_after: experiment-migrate", "migrate_after: experiment-service")
        self.assert_error("migrate_after experiment-service is not a migrate job")

    def test_matrix_entry_outside_build_job_does_not_count(self) -> None:
        text = self.paths.deploy_workflow.read_text(encoding="utf-8")
        self.paths.deploy_workflow.write_text(
            text.replace("  build-and-push:", "  decoy:\n    strategy:\n      matrix:\n        include:\n"
                         "          - service: script-service\n    steps: []\n\n  build-and-push:")
            .replace("          - service: script-service\n            context", "          - service: dropped\n            context", 1),
            encoding="utf-8",
        )
        self.assert_error("script-service: image script-service is not in the deploy.yml build matrix")

    def test_database_url_with_alternate_value_fails(self) -> None:
        self.set_env(
            self.paths.prod_compose,
            "script-service",
            "DATABASE_URL",
            "${SCRIPT_DATABASE_URL:+postgresql://u:p@h/auth_db}",
        )
        self.assert_error("script-service: in docker-compose.prod.yml DATABASE_URL must be ${SCRIPT_DATABASE_URL:?...}")

    def test_migrate_after_cycle_fails(self) -> None:
        self.edit(
            self.paths.manifest,
            "    migrate: experiment-migrate\n",
            "    migrate: experiment-migrate\n    migrate_after: telemetry-ingest-migrate\n",
        )
        self.assert_error("migrate_after forms a cycle")

    def test_prod_database_url_with_fallback_fails(self) -> None:
        self.set_env(
            self.paths.prod_compose,
            "script-service",
            "DATABASE_URL",
            "${SCRIPT_DATABASE_URL:-postgresql://u:p@postgres:5432/script_db}",
        )
        self.assert_error("script-service: in docker-compose.prod.yml DATABASE_URL must be ${SCRIPT_DATABASE_URL:?...}")

    def test_build_step_with_another_dockerfile_fails(self) -> None:
        self.edit(
            self.paths.deploy_workflow,
            "          file: ${{ matrix.context }}/${{ matrix.dockerfile }}",
            "          file: services/config-service/Dockerfile",
        )
        self.assert_error("docker/build-push-action input file must be")

    def test_meta_step_with_fixed_repository_fails(self) -> None:
        self.edit(
            self.paths.deploy_workflow,
            "          images: ${{ env.CR_REGISTRY }}/${{ matrix.service }}",
            "          images: ${{ env.CR_REGISTRY }}/fixed",
        )
        self.assert_error("docker/metadata-action input images must be")

    def test_stale_wildcard_exception_fails(self) -> None:
        compose = yaml.safe_load(self.paths.prod_compose.read_text(encoding="utf-8"))
        for spec in compose["services"].values():
            spec["environment"] = [e for e in spec.get("environment") or [] if not str(e).startswith("PGSSLROOTCERT")]
        self.paths.prod_compose.write_text(yaml.safe_dump(compose, sort_keys=False), encoding="utf-8")
        self.assert_error("env_differences.*.prod_only.PGSSLROOTCERT is allowlisted but no service differs")

    def test_shared_database_without_ordering_fails(self) -> None:
        self.edit(self.paths.manifest, "    migrate_after: experiment-migrate\n", "")
        self.assert_error("experiment_db: migrate jobs")

    def test_release_tag_rule_removed_fails(self) -> None:
        self.edit(self.paths.deploy_workflow, "            type=raw,value=${{ env.RELEASE_TAG }}\n", "")
        self.assert_error("docker/metadata-action input tags must be")

    def test_build_without_push_fails(self) -> None:
        self.edit(self.paths.deploy_workflow, "          target: production\n          push: true\n", "          target: production\n")
        self.assert_error("docker/build-push-action input push must be")

    def test_forked_migration_order_fails(self) -> None:
        manifest = yaml.safe_load(self.paths.manifest.read_text(encoding="utf-8"))
        services = manifest["services"]
        services["config-service"].update(database="experiment_db", migrate_after="experiment-migrate")
        services["script-service"].update(database="experiment_db", migrate_after="experiment-migrate")
        self.paths.manifest.write_text(yaml.safe_dump(manifest, sort_keys=False), encoding="utf-8")
        self.assert_error("experiment_db: migrate jobs")

    def test_undeclared_migration_dependency_fails(self) -> None:
        self.set_service_field(
            self.paths.prod_compose,
            "experiment-migrate",
            "depends_on",
            value={"telemetry-ingest-migrate": {"condition": "service_completed_successfully"}},
        )
        self.assert_error("experiment-migrate: in docker-compose.prod.yml waits for migrate job telemetry-ingest-migrate")

    def test_declared_migrate_without_directory_fails(self) -> None:
        shutil.rmtree(self.paths.backend_services / "auth-service" / "migrations")
        self.assert_error("auth-service: declares migrate job auth-migrate but has no migrations/ directory")

    def test_meta_step_id_changed_fails(self) -> None:
        self.edit(self.paths.deploy_workflow, "        id: meta\n", "        id: metadata\n")
        self.assert_error("docker/metadata-action step must have id 'meta'")

    def test_migrate_waiting_for_its_runtime_fails(self) -> None:
        self.set_service_field(
            self.paths.prod_compose,
            "auth-migrate",
            "depends_on",
            value={"auth-service": {"condition": "service_completed_successfully"}},
        )
        self.assert_error("auth-migrate: in docker-compose.prod.yml waits for migrate job auth-service")

    def test_registry_service_without_manifest_image_fails(self) -> None:
        manifest = yaml.safe_load(self.paths.manifest.read_text(encoding="utf-8"))
        for key in ("image", "context", "dockerfile"):
            del manifest["services"]["sensor-simulator"][key]
        self.paths.manifest.write_text(yaml.safe_dump(manifest, sort_keys=False), encoding="utf-8")
        self.assert_error("sensor-simulator: docker-compose.prod.yml uses a release-registry image")

    def test_migrate_entrypoint_override_fails(self) -> None:
        self.set_service_field(
            self.paths.prod_compose, "auth-migrate", "entrypoint", value=["sh", "-c", "exec python -m x"]
        )
        self.assert_error("auth-migrate: in docker-compose.prod.yml must not override entrypoint")

    def test_conflicting_shared_database_migrations_fail(self) -> None:
        for service, body in (("experiment-service", "SELECT 1;"), ("telemetry-ingest-service", "SELECT 2;")):
            directory = self.paths.backend_services / service / "migrations"
            directory.mkdir(parents=True, exist_ok=True)
            (directory / "006_same_name.sql").write_text(body, encoding="utf-8")
        self.assert_error("experiment_db: migration 006_same_name differs between")

    def test_identical_shared_database_migration_is_fine(self) -> None:
        for service in ("experiment-service", "telemetry-ingest-service"):
            directory = self.paths.backend_services / service / "migrations"
            directory.mkdir(parents=True, exist_ok=True)
            (directory / "006_same_name.sql").write_text("SELECT 1;", encoding="utf-8")
        self.assertEqual(check(self.paths), [])

    def test_build_step_with_if_false_fails(self) -> None:
        self.edit(
            self.paths.deploy_workflow,
            "        uses: docker/build-push-action@v6\n        with:\n          context: ${{ matrix.context }}",
            "        if: ${{ false }}\n        uses: docker/build-push-action@v6\n        with:\n          context: ${{ matrix.context }}",
        )
        self.assert_error("docker/build-push-action step must not set if")

    def test_build_step_continue_on_error_fails(self) -> None:
        self.edit(
            self.paths.deploy_workflow,
            "        uses: docker/build-push-action@v6\n        with:\n          context: ${{ matrix.context }}",
            "        continue-on-error: true\n        uses: docker/build-push-action@v6\n        with:\n          context: ${{ matrix.context }}",
        )
        self.assert_error("docker/build-push-action step must not set continue-on-error")

    def test_base_image_build_arg_removed_fails(self) -> None:
        text = self.paths.deploy_workflow.read_text(encoding="utf-8")
        marker = "          build-args: |\n            BACKEND_BASE_IMAGE=${{ env.CR_REGISTRY }}/backend-base@${{ needs.build-backend-base.outputs.digest }}\n"
        self.assertEqual(text.count(marker), 1)
        self.paths.deploy_workflow.write_text(text.replace(marker, ""), encoding="utf-8")
        self.assert_error("docker/build-push-action input build-args must be")

    def test_profiled_production_service_fails(self) -> None:
        self.set_service_field(self.paths.prod_compose, "grafana", "profiles", value=["manual"])
        self.assert_error("grafana: in docker-compose.prod.yml has profiles")

    def test_dev_only_service_in_prod_fails(self) -> None:
        self.edit(
            self.paths.prod_compose,
            "\n  loki:\n",
            "\n  mailpit:\n    image: axllent/mailpit\n\n  loki:\n",
        )
        self.assert_error("mailpit: listed in dev_only_services but present in docker-compose.prod.yml")


if __name__ == "__main__":
    unittest.main()
