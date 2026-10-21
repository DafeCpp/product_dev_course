"""Schema management with additive-only compat checker."""
from __future__ import annotations

from typing import Any, cast
from collections.abc import Iterable

import structlog
from referencing import Registry, Resource
from referencing.exceptions import Unresolvable
from referencing.jsonschema import specification_with, UnknownDialect

from config_service.core.exceptions import (
    InvalidSchemaError,
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




class _SchemaResource:
    """Resource adapter for referencing's legacy dependencies enumeration bug.

    Delegate reference/anchor semantics to Resource, retaining original contents.
    Both our preflight and Registry.crawl use the corrected child enumeration.
    Composition is intentional: referencing does not support subclassing.
    """

    def __init__(self, resource: Resource[Any], validator: Any) -> None:
        self.resource = resource
        self.validator = validator

    def __getattr__(self, name: str) -> Any:
        return getattr(self.resource, name)

    def subresources(self) -> Iterable[Resource[Any]]:
        import jsonschema.validators

        contents = self.resource.contents
        validator = jsonschema.validators.validator_for(contents, default=self.validator)  # type: ignore[no-untyped-call, unused-ignore]
        traversal = contents
        if isinstance(contents, dict) and "dependencies" in validator.VALIDATORS:
            traversal = {**contents, "dependencies": {
                name: dependency for name, dependency in contents.get("dependencies", {}).items()
                if isinstance(dependency, dict)
            }}
        specification = specification_with(
            validator.META_SCHEMA.get("$id") or validator.META_SCHEMA["id"]
        )
        for child in Resource.from_contents(traversal, default_specification=specification).subresources():
            yield cast(Resource[Any], _SchemaResource(child, validator))


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
        import jsonschema
        import jsonschema.validators

        if "$schema" in new_schema and not isinstance(new_schema["$schema"], str):
            raise InvalidSchemaError("$schema must be a string")

        if "$schema" in new_schema:
            validator_cls = jsonschema.validators.validator_for(new_schema, default=None)  # type: ignore[arg-type, no-untyped-call, unused-ignore]
            if validator_cls is None:
                raise InvalidSchemaError(f"Unsupported $schema: {new_schema['$schema']}")
        else:
            validator_cls = jsonschema.validators.validator_for(new_schema)  # type: ignore[no-untyped-call, unused-ignore]
        try:
            validator_cls.check_schema(new_schema)
        except jsonschema.exceptions.SchemaError as exc:
            raise InvalidSchemaError(exc.message) from exc
        # Resolve references without network access, including nested resource IDs.
        # Traverse schema keywords only: examples/default/const contain instance data.
        specification = specification_with(
            validator_cls.META_SCHEMA["$id"]
            if "$id" in validator_cls.META_SCHEMA else validator_cls.META_SCHEMA["id"]
        )
        resource = cast(Resource[Any], _SchemaResource(
            specification.create_resource(new_schema), validator_cls
        ))
        registry: Registry[Any] = Registry()
        pending = [(resource, registry.resolver_with_root(resource), validator_cls)]
        visited: set[tuple[int, type[Any]]] = set()
        try:
            while pending:
                current_resource, resolver, inherited_validator = pending.pop()
                contents = current_resource.contents
                current_validator = jsonschema.validators.validator_for(contents, default=inherited_validator)  # type: ignore[no-untyped-call, unused-ignore]
                if isinstance(contents, dict) and "$schema" in contents:
                    current_validator = jsonschema.validators.validator_for(contents, default=None)  # type: ignore[arg-type, no-untyped-call, unused-ignore]
                    if current_validator is None:
                        raise InvalidSchemaError(f"Unsupported $schema: {contents['$schema']}")
                current_validator.check_schema(contents)
                key = (id(contents), current_validator)
                if key in visited:
                    continue
                visited.add(key)
                if isinstance(contents, dict):
                    for keyword in ("$ref", "$dynamicRef", "$recursiveRef"):
                        if keyword in contents and keyword in current_validator.VALIDATORS:
                            resolved = resolver.lookup(contents[keyword])
                            if not isinstance(resolved.contents, (dict, bool)):
                                raise InvalidSchemaError("Reference target must be a schema object or boolean")
                            target = Resource.from_contents(resolved.contents, default_specification=specification_with(
                                current_validator.META_SCHEMA.get("$id") or current_validator.META_SCHEMA["id"]
                            ))
                            pending.append((target, resolved.resolver, current_validator))
                pending.extend(
                    (child, resolver.in_subresource(child), current_validator)
                    for child in cast(Resource[Any], _SchemaResource(current_resource, current_validator)).subresources()
                )
            registry = registry.with_resource(resource.id() or "", resource).crawl()
        except (Unresolvable, UnknownDialect, ValueError, jsonschema.exceptions.SchemaError) as exc:
            raise InvalidSchemaError(str(exc)) from exc
        validator = validator_cls(new_schema, registry=registry)

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
