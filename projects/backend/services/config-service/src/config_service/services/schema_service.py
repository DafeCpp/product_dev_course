"""Schema management with additive-only compat checker."""
from __future__ import annotations

from typing import Any

import structlog

from config_service.core.exceptions import (
    SchemaBreakingChangeError,
    SchemaSanityFailedError,
)
from config_service.domain.enums import ConfigType
from config_service.domain.models import ConfigSchema
from config_service.prometheus_metrics import (
    config_compat_check_rejections_total,
    config_sanity_check_failures_total,
)
from config_service.repositories.config_repo import ConfigRepository
from config_service.repositories.schema_repo import SchemaRepository

logger = structlog.get_logger(__name__)

# Keys whose tightening is a breaking change
_NUMERIC_BOUNDS = {
    "maximum": lambda old, new: new < old,   # lowering maximum is breaking
    "minimum": lambda old, new: new > old,   # raising minimum is breaking
    "maxLength": lambda old, new: new < old,
    "minLength": lambda old, new: new > old,
}


def _check_compat(old_schema: dict[str, Any], new_schema: dict[str, Any]) -> list[str]:
    """Return list of breaking change descriptions (empty = all additive)."""
    violations: list[str] = []

    old_required = set(old_schema.get("required", []))
    new_required = set(new_schema.get("required", []))
    added_required = new_required - old_required
    if added_required:
        violations.append(f"New required fields added: {sorted(added_required)}")

    old_props = old_schema.get("properties", {})
    new_props = new_schema.get("properties", {})

    for field in old_props:
        if field not in new_props:
            violations.append(f"Field removed: '{field}'")
            continue
        old_f = old_props[field]
        new_f = new_props[field]

        old_type = old_f.get("type")
        new_type = new_f.get("type")
        if old_type and new_type and old_type != new_type:
            violations.append(f"Field '{field}' type changed: {old_type} -> {new_type}")

        for bound_key, is_breaking in _NUMERIC_BOUNDS.items():
            old_val = old_f.get(bound_key)
            new_val = new_f.get(bound_key)
            if old_val is not None and new_val is not None and is_breaking(old_val, new_val):  # type: ignore[no-untyped-call]
                violations.append(
                    f"Field '{field}' {bound_key} tightened: {old_val} -> {new_val}"
                )

        old_enum = old_f.get("enum")
        new_enum = new_f.get("enum")
        if old_enum is not None and new_enum is not None:
            removed = set(old_enum) - set(new_enum)
            if removed:
                violations.append(f"Field '{field}' enum values removed: {sorted(removed)}")

    return violations


class SchemaService:
    def __init__(
        self,
        schema_repo: SchemaRepository,
        config_repo: ConfigRepository,
    ) -> None:
        self._schema_repo = schema_repo
        self._config_repo = config_repo

    async def get_active(self, config_type: ConfigType) -> ConfigSchema | None:
        return await self._schema_repo.get_active(config_type)

    async def list_active(self) -> list[ConfigSchema]:
        return await self._schema_repo.list_active()

    async def list_history(self, config_type: ConfigType) -> list[ConfigSchema]:
        return await self._schema_repo.list_history(config_type)

    async def update(
        self,
        config_type: ConfigType,
        new_schema: dict[str, Any],
        created_by: str,
    ) -> ConfigSchema:
        current = await self._schema_repo.get_active(config_type)

        if current is not None:
            violations = _check_compat(current.schema, new_schema)
            if violations:
                for v in violations:
                    config_compat_check_rejections_total.labels(
                        config_type=config_type.value,
                        rule=v[:64],
                    ).inc()
                raise SchemaBreakingChangeError(violations)

        import jsonschema
        import jsonschema.validators

        validator_cls = jsonschema.validators.validator_for(new_schema)  # type: ignore[no-untyped-call, unused-ignore]
        validator = validator_cls(new_schema)

        existing_configs = await self._config_repo.list_by_type(config_type)
        failures: list[dict[str, Any]] = []
        for cfg in existing_configs:
            errs = [e.message for e in validator.iter_errors(cfg.value)]
            if errs:
                failures.append({"config_id": str(cfg.id), "key": cfg.key, "errors": errs})

        if failures:
            config_sanity_check_failures_total.labels(config_type=config_type.value).inc()
            logger.error(
                "schema_sanity_check_failed",
                config_type=config_type.value,
                failures=failures,
            )
            raise SchemaSanityFailedError(failures)

        result = await self._schema_repo.insert_new_version_and_activate(
            config_type, new_schema, created_by
        )
        logger.info(
            "schema_updated",
            config_type=config_type.value,
            version=result.version,
            updated_by=created_by,
        )
        return result
