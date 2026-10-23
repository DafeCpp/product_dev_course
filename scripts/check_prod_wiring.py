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
    root: Path
    manifest: Path
    dev_compose: Path
    prod_compose: Path
    deploy_workflow: Path
    terraform_database: Path
    backend_services: Path

    @classmethod
    def under(cls, root: Path) -> Paths:
        return cls(
            root=root,
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


def _env_value(service: dict[str, Any], key: str) -> str | None:
    env = service.get("environment") or []
    if isinstance(env, dict):
        value = env.get(key)
        return None if value is None else str(value)
    for item in env:
        name, sep, value = str(item).partition("=")
        if name == key and sep:
            return value
    return None


def _database_env_var(database: str) -> str:
    """auth_db -> AUTH_DATABASE_URL, the .env key that holds the database's DSN."""
    return f"{database.removesuffix('_db').upper()}_DATABASE_URL"


def _check_database_url(
    name: str, label: str, service: dict[str, Any], database: str
) -> list[str]:
    is_prod = label == "docker-compose.prod.yml"
    value = _env_value(service, "DATABASE_URL")
    var = _database_env_var(database)
    if value is None:
        return [f"{name}: in {label} has no DATABASE_URL for {database}"]
    errors = []
    env = service.get("environment") or []
    if isinstance(env, list) and sum(str(i).partition("=")[0] == "DATABASE_URL" for i in env) > 1:
        # Compose keeps the last duplicate, but the checker would read the first.
        errors.append(f"{name}: in {label} declares DATABASE_URL more than once")
    # Production must require the variable (${VAR:?error}): a fallback DSN would let a
    # missing variable pass pre-flight. Dev uses ${VAR:-default}. ${VAR:+alt} is never valid.
    operator = "?" if is_prod else "-"
    if not re.fullmatch(rf"\$\{{{var}:\{operator}[^}}]*\}}", value):
        errors.append(
            f"{name}: in {label} DATABASE_URL must be ${{{var}:{operator}...}} ({database}), got {value!r}"
        )
    # The dev default DSN must point at the same database as the variable.
    default = re.match(r"\$\{[A-Z0-9_]+:-(?P<dsn>[^}]*)\}$", value)
    if default and not re.search(rf"/{re.escape(database)}(\?|$)", default.group("dsn")):
        errors.append(f"{name}: in {label} the default DATABASE_URL does not point at {database}")
    return errors


def _replicas(service: dict[str, Any]) -> int | None:
    deploy_replicas = (service.get("deploy") or {}).get("replicas")
    scale = service.get("scale")
    for value in (scale, deploy_replicas):
        if isinstance(value, int) and value == 0:
            return 0
    return None


def _depends_on(service: dict[str, Any]) -> dict[str, str | None]:
    deps = service.get("depends_on") or {}
    if isinstance(deps, list):
        return {name: None for name in deps}
    result: dict[str, str | None] = {}
    for name, spec in deps.items():
        spec = spec or {}
        condition = spec.get("condition")
        # required: false only warns when the dependency fails, so it does not block the service.
        result[name] = f"optional {condition}" if spec.get("required") is False else condition
    return result


BUILD_JOB = "build-and-push"


def _matrix_entries(workflow: dict[str, Any]) -> tuple[dict[str, dict[str, Any]], list[str]]:
    """The release build matrix as ({image: {context, dockerfile}}, duplicated images)."""
    entries: dict[str, dict[str, Any]] = {}
    duplicates: list[str] = []
    for job_id, job in (workflow.get("jobs") or {}).items():
        if job_id != BUILD_JOB:
            continue
        include = ((job or {}).get("strategy") or {}).get("matrix", {}).get("include") or []
        for item in include:
            if isinstance(item, dict) and "service" in item:
                image = str(item["service"])
                if image in entries and image not in duplicates:
                    duplicates.append(image)
                entries[image] = item
    return entries, duplicates


def _released_image(image: str) -> str:
    """The exact reference docker-compose.prod.yml must use for an image deploy.yml publishes."""
    return f"${{CR_REGISTRY:?CR_REGISTRY must be set in production .env}}/{image}:${{IMAGE_TAG:-latest}}"


MIGRATE_COMMAND = ["python", "-m", "bin.migrate"]


def _check_migrate_job(name: str, label: str, job: dict[str, Any]) -> list[str]:
    """A migrate job must be the one-shot migration, or the runtime would wait for it forever."""
    errors = []
    if job.get("command") != MIGRATE_COMMAND:
        errors.append(
            f"{name}: in {label} command must be {MIGRATE_COMMAND}, got {job.get('command')!r}"
        )
    if job.get("entrypoint"):
        errors.append(f"{name}: in {label} must not override entrypoint, got {job['entrypoint']!r}")
    if str(job.get("restart", "no")) != "no":
        errors.append(f"{name}: in {label} restart must be 'no' for a one-shot job")
    return errors


def _build_steps(workflow: dict[str, Any]) -> list[dict[str, Any]]:
    job = (workflow.get("jobs") or {}).get(BUILD_JOB) or {}
    return [s for s in job.get("steps") or [] if isinstance(s, dict)]


# What the build job must pass to its actions so they use the matrix entry's own fields.
_BUILD_WIRING = {
    "docker/metadata-action": {
        "images": "${{ env.CR_REGISTRY }}/${{ matrix.service }}",
        "tags": "type=raw,value=${{ env.RELEASE_TAG }}\ntype=raw,value=latest",
    },
    "docker/build-push-action": {
        "context": "${{ matrix.context }}",
        "file": "${{ matrix.context }}/${{ matrix.dockerfile }}",
        "target": "production",
        "tags": "${{ steps.meta.outputs.tags }}",
        "push": "true",
        "build-args": "BACKEND_BASE_IMAGE=${{ env.CR_REGISTRY }}/backend-base@${{ needs.build-backend-base.outputs.digest }}",
    },
}


def _check_build_steps(workflow: dict[str, Any]) -> list[str]:
    errors = []
    steps = _build_steps(workflow)
    job = (workflow.get("jobs") or {}).get(BUILD_JOB) or {}
    needs = job.get("needs") or []
    if "build-backend-base" not in ([needs] if isinstance(needs, str) else needs):
        errors.append(
            f"deploy.yml {BUILD_JOB} must directly need build-backend-base, "
            "or its digest output is unavailable to the build"
        )
    for guard in ("if", "continue-on-error"):
        if guard in job:
            errors.append(f"deploy.yml {BUILD_JOB} job must not set {guard}")
    for action, expected in _BUILD_WIRING.items():
        found = [s for s in steps if str(s.get("uses", "")).split("@")[0] == action]
        if len(found) != 1:
            errors.append(f"deploy.yml {BUILD_JOB} must have exactly one {action} step, found {len(found)}")
            continue
        for guard in ("if", "continue-on-error"):
            if guard in found[0]:
                errors.append(
                    f"deploy.yml {BUILD_JOB} {action} step must not set {guard}, "
                    "or the release could skip or ignore it"
                )
        if action == "docker/metadata-action" and found[0].get("id") != "meta":
            errors.append(
                f"deploy.yml {BUILD_JOB} {action} step must have id 'meta', got {found[0].get('id')!r}"
            )
        with_ = found[0].get("with") or {}
        for key, value in expected.items():
            raw = with_.get(key, "")
            text = str(raw).lower() if isinstance(raw, bool) else str(raw).strip()
            actual = "\n".join(line.strip() for line in text.splitlines())
            if actual != value:
                errors.append(
                    f"deploy.yml {BUILD_JOB} {action} input {key} must be {value!r}, "
                    f"got {with_.get(key)!r}"
                )
    return errors


_TERRAFORM_TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|/\*.*?\*/|//[^\n]*|#[^\n]*', re.DOTALL)


def _strip_terraform_comments(terraform: str) -> str:
    """Drop #, // and /* */ comments, keeping string literals (a "#" inside one is not a comment)."""
    return _TERRAFORM_TOKEN.sub(
        lambda m: m.group(0) if m.group(0).startswith('"') else "", terraform
    )


BASE_JOB = "build-backend-base"
BASE_GATES = ("backend-tests", "frontend-tests", "production-contract-tests")

_BASE_WIRING = {
    "context": "./projects/backend",
    "file": "./projects/backend/Dockerfile.base",
    "target": "base",
    "push": "true",
    "tags": (
        "${{ env.CR_REGISTRY }}/backend-base:${{ env.RELEASE_TAG }}\n"
        "${{ env.CR_REGISTRY }}/backend-base:latest"
    ),
}
_BASE_DIGEST = "${{ steps.build.outputs.digest }}"


def _normalized(raw: Any) -> str:
    text = str(raw).lower() if isinstance(raw, bool) else str(raw).strip()
    return "\n".join(line.strip() for line in text.splitlines())


def _check_base_job(workflow: dict[str, Any], root: Path) -> list[str]:
    """build-and-push pins the base by this job's digest, so the job must really publish it."""
    job = (workflow.get("jobs") or {}).get(BASE_JOB)
    if not isinstance(job, dict):
        return [f"deploy.yml must have a {BASE_JOB} job that publishes the backend base image"]
    errors = []
    needs = job.get("needs") or []
    needs = [needs] if isinstance(needs, str) else needs
    for gate in BASE_GATES:
        if gate not in needs:
            errors.append(
                f"deploy.yml {BASE_JOB} must directly need {gate}, "
                "or images are published while the test gate is still running or failing"
            )
    for guard in ("if", "continue-on-error"):
        if guard in job:
            errors.append(f"deploy.yml {BASE_JOB} job must not set {guard}")
    digest = (job.get("outputs") or {}).get("digest")
    if _normalized(digest) != _BASE_DIGEST:
        errors.append(f"deploy.yml {BASE_JOB} output digest must be {_BASE_DIGEST!r}, got {digest!r}")
    steps = [s for s in job.get("steps") or [] if isinstance(s, dict)]
    found = [s for s in steps if str(s.get("uses", "")).split("@")[0] == "docker/build-push-action"]
    if len(found) != 1 or found[0].get("id") != "build":
        errors.append(
            f"deploy.yml {BASE_JOB} must have exactly one docker/build-push-action step, "
            f"with id 'build'; found {len(found)}"
        )
        return errors
    step = found[0]
    for guard in ("if", "continue-on-error"):
        if guard in step:
            errors.append(f"deploy.yml {BASE_JOB} build step must not set {guard}")
    with_ = step.get("with") or {}
    for key, value in _BASE_WIRING.items():
        if _normalized(with_.get(key, "")) != value:
            errors.append(
                f"deploy.yml {BASE_JOB} build input {key} must be {value!r}, got {with_.get(key)!r}"
            )
    dockerfile = root / "projects" / "backend" / "Dockerfile.base"
    if not dockerfile.is_file():
        errors.append(f"deploy.yml {BASE_JOB} builds projects/backend/Dockerfile.base, which does not exist")
    return errors


def _terraform_databases(terraform: str) -> set[str]:
    terraform = _strip_terraform_comments(terraform)
    blocks = re.findall(
        r'resource "yandex_mdb_postgresql_database" "[^"]+" \{(.*?)\n\}', terraform, re.DOTALL
    )
    names: set[str] = set()
    for body in blocks:
        match = re.search(r'(?m)^\s*name\s*=\s*"([^"]+)"', body)
        if match:
            names.add(match.group(1))
    return names


def _build_source(build: Any) -> tuple[str | None, str | None]:
    if isinstance(build, str):
        return build, None
    if isinstance(build, dict):
        return build.get("context"), build.get("dockerfile")
    return None, None


def _check_build_source(
    name: str,
    spec: dict[str, Any],
    matrix_entry: dict[str, Any] | None,
    dev: dict[str, dict[str, Any]],
    prod: dict[str, dict[str, Any]],
    root: Path,
) -> list[str]:
    """The release matrix, dev builds and prod migrate image must all be this service's own."""
    expected = (spec.get("context"), spec.get("dockerfile"))
    errors: list[str] = []
    if None in expected:
        return [f"{name}: manifest needs context and dockerfile for image {spec['image']}"]
    context_dir = root / str(expected[0])
    if not context_dir.is_dir():
        errors.append(f"{name}: build context {expected[0]} does not exist")
    elif not (context_dir / str(expected[1])).is_file():
        errors.append(f"{name}: Dockerfile {expected[1]} does not exist in build context {expected[0]}")
    if matrix_entry is not None:
        actual = (matrix_entry.get("context"), matrix_entry.get("dockerfile"))
        if actual != expected:
            errors.append(
                f"{name}: deploy.yml matrix builds {actual[1]} from {actual[0]}, "
                f"expected {expected[1]} from {expected[0]}"
            )
    for target in filter(None, (name, spec.get("migrate"))):
        if target in dev:
            actual = _build_source(dev[target].get("build"))
            if actual != expected:
                errors.append(
                    f"{target}: docker-compose.yml builds {actual[1]} from {actual[0]}, "
                    f"expected {expected[1]} from {expected[0]}"
                )
    migrate = spec.get("migrate")
    if migrate and migrate in prod and name in prod:
        if prod[migrate].get("image") != prod[name].get("image"):
            errors.append(
                f"{migrate}: docker-compose.prod.yml image {prod[migrate].get('image')!r} "
                f"differs from {name} image {prod[name].get('image')!r}"
            )
    return errors


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
    workflow = _load_yaml(paths.deploy_workflow)
    matrix_entries, matrix_duplicates = _matrix_entries(workflow)
    matrix = set(matrix_entries)
    databases = _terraform_databases(paths.terraform_database.read_text(encoding="utf-8"))

    errors: list[str] = _check_build_steps(workflow)
    errors += _check_base_job(workflow, paths.root)
    errors += [
        f"deploy.yml build matrix lists {image} more than once; both jobs would push the same tags"
        for image in sorted(matrix_duplicates)
    ]
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
            elif _replicas(compose[name]) == 0:
                errors.append(f"{name}: in {label} is scaled to 0, so no container runs")
            elif compose[name].get("profiles"):
                errors.append(
                    f"{name}: in {label} has profiles, so a plain `docker compose up -d` skips it"
                )

    for name in sorted(known):
        if name in dev and name in prod:
            errors.extend(_check_env(name, _env_keys(dev[name]), _env_keys(prod[name]), differences))
    shared = differences.get("*") or {}
    for side in ("dev_only", "prod_only"):
        for key in sorted(set(shared.get(side) or {})):
            if not any(
                key in (_env_keys(dev[n]) ^ _env_keys(prod[n]))
                and ((key in _env_keys(dev[n])) == (side == "dev_only"))
                for n in known
                if n in dev and n in prod
            ):
                errors.append(
                    f"env_differences.*.{side}.{key} is allowlisted but no service differs; remove it"
                )
    for name in sorted(set(differences) - known - {"*"}):
        errors.append(f"env_differences.{name}: not a service in the manifest")

    for name, spec in sorted(wanted.items()):
        image = spec.get("image")
        if not image and "CR_REGISTRY" in str(prod.get(name, {}).get("image", "")):
            errors.append(
                f"{name}: docker-compose.prod.yml uses a release-registry image, "
                "so the manifest must declare image, context and dockerfile"
            )
        if image:
            if image not in matrix:
                errors.append(f"{name}: image {image} is not in the deploy.yml build matrix")
            prod_image = str(prod.get(name, {}).get("image", ""))
            if name in prod and prod_image != _released_image(image):
                errors.append(
                    f"{name}: prod image {prod_image!r} is not the released image "
                    f"{_released_image(image)!r}"
                )
            errors.extend(_check_build_source(name, spec, matrix_entries.get(image), dev, prod, paths.root))

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
            for label, compose in (("docker-compose.prod.yml", prod), ("docker-compose.yml", dev)):
                if migrate in compose:
                    errors.extend(_check_migrate_job(migrate, label, compose[migrate]))
            after = spec.get("migrate_after")
            if after and after not in migrates:
                errors.append(f"{name}: migrate_after {after} is not a migrate job in the manifest")
            elif after == migrate:
                errors.append(f"{name}: migrate_after must not be its own migrate job {migrate}")
            elif after:
                for label, compose in (("docker-compose.prod.yml", prod), ("docker-compose.yml", dev)):
                    if migrate in compose:
                        condition = _depends_on(compose[migrate]).get(after)
                        if condition != "service_completed_successfully":
                            errors.append(
                                f"{migrate}: in {label} does not wait for {after} "
                                "(depends_on condition service_completed_successfully)"
                            )
            if not (paths.backend_services / name / "migrations").is_dir():
                errors.append(f"{name}: declares migrate job {migrate} but has no migrations/ directory")
            declared = {spec["migrate_after"]} if spec.get("migrate_after") else set()
            for label, compose in (("docker-compose.prod.yml", prod), ("docker-compose.yml", dev)):
                if migrate in compose:
                    waits = set(_depends_on(compose[migrate])) & known
                    for extra in sorted(waits - declared):
                        errors.append(
                            f"{migrate}: in {label} waits for migrate job {extra}, "
                            "which the manifest does not declare in migrate_after"
                        )
            if not spec.get("database"):
                errors.append(f"{name}: has a migrate job, so the manifest must declare its database")
        elif (paths.backend_services / name / "migrations").is_dir():
            errors.append(f"{name}: has migrations/ but no migrate service in the manifest")

        database = spec.get("database")
        if database:
            if database not in databases:
                errors.append(f"{name}: database {database} is not created in Terraform database.tf")
            for target in filter(None, (name, migrate)):
                for label, compose in (("docker-compose.prod.yml", prod), ("docker-compose.yml", dev)):
                    if target in compose:
                        errors.extend(_check_database_url(target, label, compose[target], database))

    order = {
        spec["migrate"]: spec["migrate_after"]
        for spec in wanted.values()
        if spec.get("migrate") and spec.get("migrate_after") in migrates
    }
    by_database: dict[str, list[str]] = {}
    for spec in wanted.values():
        if spec.get("migrate") and spec.get("database"):
            by_database.setdefault(spec["database"], []).append(spec["migrate"])
    for database, jobs in sorted(by_database.items()):
        # One chain: all but the first job wait for another job on the same database.
        unordered = [j for j in jobs if order.get(j) not in jobs]
        predecessors = [order[j] for j in jobs if order.get(j) in jobs]
        forked = len(predecessors) != len(set(predecessors))
        if len(jobs) > 1 and (len(unordered) != 1 or forked):
            errors.append(
                f"{database}: migrate jobs {sorted(jobs)} share it but are not run one after another; "
                "set migrate_after so they form one chain (one first job, each job awaited by at most one)"
            )
    for database, jobs in sorted(by_database.items()):
        stems: dict[str, tuple[str, bytes]] = {}
        for spec_name, spec in sorted(wanted.items()):
            if spec.get("database") != database or not spec.get("migrate"):
                continue
            for sql in sorted((paths.backend_services / spec_name / "migrations").glob("*.sql")):
                content = sql.read_bytes()
                if sql.stem in stems and stems[sql.stem][1] != content:
                    errors.append(
                        f"{database}: migration {sql.stem} differs between {stems[sql.stem][0]} "
                        f"and {spec_name}; the shared schema_migrations table would reject the second"
                    )
                stems.setdefault(sql.stem, (spec_name, content))
    for job in sorted(order):
        seen = [job]
        while seen[-1] in order:
            nxt = order[seen[-1]]
            if nxt in seen:
                errors.append(f"{job}: migrate_after forms a cycle: {' -> '.join(seen + [nxt])}")
                break
            seen.append(nxt)

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
