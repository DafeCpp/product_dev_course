"""Compile schemas consistently for activation and config reads/writes."""
from __future__ import annotations

from collections.abc import Iterable
from typing import Any, cast

import jsonschema
import jsonschema.validators
from referencing import Registry, Resource
from referencing.exceptions import Unresolvable
from referencing.jsonschema import UnknownDialect, specification_with
from rfc3986_validator import validate_rfc3986

from config_service.core.exceptions import InvalidSchemaError


def _check_schema_node(schema: Any, validator: Any) -> None:
    """Check one schema node without imposing its dialect on child resources.

    Child schema objects are checked separately by the preflight traversal.
    Keep non-object children intact so malformed keyword containers and scalar
    schema values are still rejected by the selected metaschema.
    """
    specification = specification_with(
        validator.META_SCHEMA.get("$id") or validator.META_SCHEMA["id"]
    )
    try:
        traversal = schema
        if isinstance(schema, dict) and "dependencies" in validator.VALIDATORS:
            traversal = {**schema, "dependencies": {
                name: child for name, child in schema.get("dependencies", {}).items()
                if isinstance(child, dict)
            }}
        if specification.name == "draft-03" and isinstance(traversal, dict):
            traversal = {key: value for key, value in traversal.items() if key != "definitions"}
        children = list(specification.subresources_of(traversal))
    except (AttributeError, TypeError):
        validator.check_schema(schema)
        return
    child_ids = {id(child) for child in children if isinstance(child, dict)}

    def view(value: Any) -> Any:
        if id(value) in child_ids:
            return {}
        if isinstance(value, dict):
            return {key: view(item) for key, item in value.items()}
        if isinstance(value, list):
            return [view(item) for item in value]
        return value

    validator.check_schema(view(schema))
    if isinstance(schema, dict):
        keyword = "id" if specification.name in {"draft-03", "draft-04"} else "$id"
        if keyword in schema and not validate_rfc3986(schema[keyword], rule="URI_reference"):
            raise InvalidSchemaError(f"Invalid URI-reference in {keyword}: {schema[keyword]}")


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
        if specification.name == "draft-03" and isinstance(traversal, dict):
            traversal = {key: value for key, value in traversal.items() if key != "definitions"}
        for contents in specification.subresources_of(traversal):
            child_validator = validator
            if isinstance(contents, dict) and "$schema" in contents:
                if not isinstance(contents["$schema"], str):
                    raise InvalidSchemaError("$schema must be a string")
                child_validator = jsonschema.validators.validator_for(contents, default=None)  # type: ignore[arg-type, no-untyped-call, unused-ignore]
                if child_validator is None:
                    raise InvalidSchemaError(f"Unsupported $schema: {contents['$schema']}")
            _check_schema_node(contents, child_validator)
            child = Resource.from_contents(contents, default_specification=specification)
            yield cast(Resource[Any], _SchemaResource(child, child_validator))


def build_schema_validator(new_schema: dict[str, Any]) -> jsonschema.protocols.Validator:
    if "$schema" in new_schema and not isinstance(new_schema["$schema"], str):
        raise InvalidSchemaError("$schema must be a string")

    if "$schema" in new_schema:
        validator_cls = jsonschema.validators.validator_for(new_schema, default=None)  # type: ignore[arg-type, no-untyped-call, unused-ignore]
        if validator_cls is None:
            raise InvalidSchemaError(f"Unsupported $schema: {new_schema['$schema']}")
    else:
        validator_cls = jsonschema.validators.validator_for(new_schema)  # type: ignore[no-untyped-call, unused-ignore]
    try:
        _check_schema_node(new_schema, validator_cls)
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
            _check_schema_node(contents, current_validator)
            key = (id(contents), current_validator)
            if key in visited:
                continue
            visited.add(key)
            if isinstance(contents, dict):
                for keyword in ("$ref", "$dynamicRef", "$recursiveRef"):
                    if keyword in contents and keyword in current_validator.VALIDATORS:
                        if not isinstance(contents[keyword], str):
                            raise InvalidSchemaError(f"{keyword} must be a string")
                        if not validate_rfc3986(contents[keyword], rule="URI_reference"):
                            raise InvalidSchemaError(f"Invalid URI-reference in {keyword}: {contents[keyword]}")
                        resolved = resolver.lookup(contents[keyword])
                        if not isinstance(resolved.contents, (dict, bool)):
                            raise InvalidSchemaError("Reference target must be a schema object or boolean")
                        target_root = resolved.resolver.lookup("").contents
                        target_validator = jsonschema.validators.validator_for(target_root, default=current_validator)  # type: ignore[no-untyped-call, unused-ignore]
                        if isinstance(resolved.contents, dict) and "$schema" in resolved.contents:
                            if not isinstance(resolved.contents["$schema"], str):
                                raise InvalidSchemaError("$schema must be a string")
                            target_validator = jsonschema.validators.validator_for(resolved.contents, default=None)  # type: ignore[arg-type, no-untyped-call, unused-ignore]
                            if target_validator is None:
                                raise InvalidSchemaError(f"Unsupported $schema: {resolved.contents['$schema']}")
                        _check_schema_node(resolved.contents, target_validator)
                        target = Resource.from_contents(resolved.contents, default_specification=specification_with(
                            target_validator.META_SCHEMA.get("$id") or target_validator.META_SCHEMA["id"]
                        ))
                        pending.append((target, resolved.resolver, target_validator))
            pending.extend(
                (child, resolver.in_subresource(child), current_validator)
                for child in cast(Resource[Any], _SchemaResource(current_resource, current_validator)).subresources()
            )
        registry = registry.with_resource(resource.id() or "", resource).crawl()
    except (Unresolvable, UnknownDialect, ValueError, jsonschema.exceptions.SchemaError) as exc:
        raise InvalidSchemaError(str(exc)) from exc
    return validator_cls(new_schema, registry=registry)
