#!/usr/bin/env python3
"""Check dev/prod compose drift and production wiring against a manifest (LOS-112).

Compares infrastructure/production-wiring.yml with docker-compose.yml,
docker-compose.prod.yml, the release build matrix in .github/workflows/deploy.yml
and the Terraform databases. Prints every problem and exits 1 if there is any.

Usage: python3 scripts/check_prod_wiring.py [--root DIR]
"""
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import yaml

ROOT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class Paths:
    manifest: Path
    dev_compose: Path
    prod_compose: Path
    deploy_workflow: Path
    terraform_database: Path
    backend_services: Path

    @classmethod
    def under(cls, root: Path) -> Paths:
        return cls(
            manifest=root / "infrastructure" / "production-wiring.yml",
            dev_compose=root / "docker-compose.yml",
            prod_compose=root / "docker-compose.prod.yml",
            deploy_workflow=root / ".github" / "workflows" / "deploy.yml",
            terraform_database=root / "infrastructure" / "yandex-cloud" / "database.tf",
            backend_services=root / "projects" / "backend" / "services",
        )


def _load_yaml(path: Path) -> dict[str, Any]:
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    return data if isinstance(data, dict) else {}


def _services(compose: dict[str, Any]) -> dict[str, dict[str, Any]]:
    services = compose.get("services") or {}
    return {name: spec or {} for name, spec in services.items()}


def _env_keys(service: dict[str, Any]) -> set[str]:
    env = service.get("environment") or []
    if isinstance(env, dict):
        return set(env)
    return {str(item).split("=", 1)[0] for item in env}


def _depends_on(service: dict[str, Any]) -> dict[str, str | None]:
    deps = service.get("depends_on") or {}
    if isinstance(deps, list):
        return {name: None for name in deps}
    return {name: (spec or {}).get("condition") for name, spec in deps.items()}


def _matrix_images(workflow: str) -> set[str]:
    return set(re.findall(r"(?m)^\s*-\s*service:\s*([A-Za-z0-9_.-]+)\s*$", workflow))


def _terraform_databases(terraform: str) -> set[str]:
    blocks = re.findall(
        r'resource "yandex_mdb_postgresql_database" "[^"]+" \{(.*?)\n\}', terraform, re.DOTALL
    )
    names: set[str] = set()
    for body in blocks:
        match = re.search(r'(?m)^\s*name\s*=\s*"([^"]+)"', body)
        if match:
            names.add(match.group(1))
    return names


def _check_env(
    name: str,
    dev_keys: set[str],
    prod_keys: set[str],
    differences: dict[str, Any],
) -> list[str]:
    shared = differences.get("*") or {}
    own = differences.get(name) or {}
    errors: list[str] = []
    for side, actual in (("dev_only", dev_keys - prod_keys), ("prod_only", prod_keys - dev_keys)):
        allowed_own = set(own.get(side) or {})
        allowed = allowed_own | set(shared.get(side) or {})
        where = "docker-compose.yml" if side == "dev_only" else "docker-compose.prod.yml"
        for key in sorted(actual - allowed):
            errors.append(
                f"{name}: env {key} is set only in {where}; add it to the other file "
                f"or allowlist it in env_differences.{name}.{side} with a reason"
            )
        # A stale entry hides the next real drift of that key, so it must go.
        for key in sorted(allowed_own - actual):
            errors.append(
                f"{name}: env_differences.{name}.{side}.{key} is allowlisted "
                "but no longer differs; remove it"
            )
    return errors


def check(paths: Paths) -> list[str]:
    manifest = _load_yaml(paths.manifest)
    wanted: dict[str, dict[str, Any]] = {
        name: spec or {} for name, spec in (manifest.get("services") or {}).items()
    }
    dev_only: dict[str, str] = manifest.get("dev_only_services") or {}
    differences: dict[str, Any] = manifest.get("env_differences") or {}

    dev = _services(_load_yaml(paths.dev_compose))
    prod = _services(_load_yaml(paths.prod_compose))
    matrix = _matrix_images(paths.deploy_workflow.read_text(encoding="utf-8"))
    databases = _terraform_databases(paths.terraform_database.read_text(encoding="utf-8"))

    errors: list[str] = []
    migrates = {spec["migrate"]: name for name, spec in wanted.items() if spec.get("migrate")}
    known = set(wanted) | set(migrates)

    for name in sorted(set(prod) - known):
        errors.append(f"{name}: in docker-compose.prod.yml but not in the manifest")
    for name in sorted(set(dev) - known - set(dev_only)):
        errors.append(
            f"{name}: in docker-compose.yml but neither in the manifest nor in dev_only_services"
        )
    for name in sorted(set(dev_only) & set(prod)):
        errors.append(f"{name}: listed in dev_only_services but present in docker-compose.prod.yml")
    for name in sorted(set(dev_only) - set(dev)):
        errors.append(f"{name}: listed in dev_only_services but missing from docker-compose.yml")

    for name in sorted(known):
        for label, compose in (("docker-compose.prod.yml", prod), ("docker-compose.yml", dev)):
            if name not in compose:
                errors.append(f"{name}: missing from {label}")

    for name in sorted(known):
        if name in dev and name in prod:
            errors.extend(_check_env(name, _env_keys(dev[name]), _env_keys(prod[name]), differences))
    for name in sorted(set(differences) - known - {"*"}):
        errors.append(f"env_differences.{name}: not a service in the manifest")

    for name, spec in sorted(wanted.items()):
        image = spec.get("image")
        if image:
            if image not in matrix:
                errors.append(f"{name}: image {image} is not in the deploy.yml build matrix")
            prod_image = str(prod.get(name, {}).get("image", ""))
            if name in prod and f"/{image}:" not in prod_image:
                errors.append(f"{name}: prod image {prod_image!r} is not the released {image} image")

        migrate = spec.get("migrate")
        if migrate:
            for label, compose in (("docker-compose.prod.yml", prod), ("docker-compose.yml", dev)):
                if name in compose and migrate in compose:
                    condition = _depends_on(compose[name]).get(migrate)
                    if condition != "service_completed_successfully":
                        errors.append(
                            f"{name}: in {label} does not wait for {migrate} "
                            "(depends_on condition service_completed_successfully)"
                        )
        elif (paths.backend_services / name / "migrations").is_dir():
            errors.append(f"{name}: has migrations/ but no migrate service in the manifest")

        database = spec.get("database")
        if database and database not in databases:
            errors.append(f"{name}: database {database} is not created in Terraform database.tf")

    released = {spec["image"] for spec in wanted.values() if spec.get("image")}
    for image in sorted(matrix - released):
        errors.append(f"deploy.yml builds {image}, but no manifest service uses it")
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root")
    args = parser.parse_args(argv)
    errors = check(Paths.under(args.root))
    if errors:
        print("Production wiring drift found:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print("Production wiring matches infrastructure/production-wiring.yml")
    return 0


if __name__ == "__main__":
    sys.exit(main())
