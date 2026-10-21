"""Compile schemas consistently for activation and config reads/writes."""
from __future__ import annotations

from collections.abc import Iterable
from typing import Any, cast

import jsonschema
import jsonschema.validators
from referencing import Registry, Resource, Specification
from referencing.exceptions import Unresolvable
from referencing.jsonschema import DynamicAnchor, UnknownDialect, specification_with
from rfc3986_validator import validate_rfc3986

from config_service.core.exceptions import InvalidSchemaError


def _schema_children(schema: Any, validator: Any) -> Iterable[Any]:
    """Enumerate schema positions, including legacy Draft 3 schema unions."""
    specification = specification_with(
        validator.META_SCHEMA.get("$id") or validator.META_SCHEMA["id"]
    )
    traversal = schema
    if isinstance(schema, dict) and "dependencies" in validator.VALIDATORS:
        traversal = {**schema, "dependencies": {
            name: child for name, child in schema.get("dependencies", {}).items()
            if isinstance(child, dict)
        }}
    if specification.name == "draft-03" and isinstance(traversal, dict):
        traversal = {key: value for key, value in traversal.items() if key != "definitions"}
        if isinstance(traversal.get("extends"), dict):
            traversal = {**traversal, "extends": [traversal["extends"]]}
    yield from specification.subresources_of(traversal)
    if specification.name == "draft-03" and isinstance(schema, dict):
        for keyword in ("type", "disallow"):
            entries = schema.get(keyword, [])
            if isinstance(entries, list):
                yield from (entry for entry in entries if isinstance(entry, dict))


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
        children = list(_schema_children(schema, validator))
    except (AttributeError, TypeError):
        validator.check_schema(schema)
        return
    placeholders: dict[int, dict[str, int]] = {}
    distinct_children: list[Any] = []
    for child in children:
        if not isinstance(child, dict):
            continue
        for index, existing in enumerate(distinct_children):
            if jsonschema.Draft202012Validator({"enum": [existing]}).is_valid(child):
                break
        else:
            index = len(distinct_children)
            distinct_children.append(child)
        placeholders[id(child)] = {"x-preflight-child": index}

    def view(value: Any) -> Any:
        if id(value) in placeholders:
            return placeholders[id(value)]
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


def _resolved_with_dialect(resolved: Any, default: Any, dialects: dict[int, Any]) -> Any:
    """Give jsonschema.evolve the target dialect without changing stored data."""
    contents = resolved.contents
    if isinstance(contents, dict) and "$schema" not in contents:
        try:
            root = resolved.resolver.lookup("").contents
        except Unresolvable:
            # A pointer can enter an ID under an unindexed extension keyword.
            root = contents
        validator = dialects.get(id(contents), dialects.get(id(root), default))
        dialect = validator.META_SCHEMA.get("$id") or validator.META_SCHEMA["id"]
        return type(resolved)(contents={**contents, "$schema": dialect}, resolver=resolved.resolver)
    return resolved


class _SchemaResolver:
    """Preserve inherited dialects for every lookup, including bare resource IDs."""

    def __init__(self, resolver: Any, validator: Any, dialects: dict[int, Any]) -> None:
        self.resolver = resolver
        self.validator = validator
        self.dialects = dialects

    def __getattr__(self, name: str) -> Any:
        return getattr(self.resolver, name)

    def lookup(self, reference: str) -> Any:
        resolved = _resolved_with_dialect(
            self.resolver.lookup(reference), self.validator, self.dialects
        )
        return type(resolved)(contents=resolved.contents, resolver=_SchemaResolver(
            resolved.resolver, self.validator, self.dialects
        ))

    def in_subresource(self, resource: Any) -> Any:
        return _SchemaResolver(
            self.resolver.in_subresource(resource), self.validator, self.dialects
        )


class _SchemaAnchor:
    def __init__(self, anchor: Any, validator: Any, dialects: dict[int, Any]) -> None:
        self.anchor = anchor
        self.name = anchor.name
        self.validator = validator
        self.dialects = dialects

    def resolve(self, resolver: Any) -> Any:
        return _resolved_with_dialect(self.anchor.resolve(resolver), self.validator, self.dialects)


class _SchemaResource:
    """Resource adapter for referencing's legacy dependencies enumeration bug.

    Delegate reference/anchor semantics to Resource, retaining original contents.
    Both our preflight and Registry.crawl use the corrected child enumeration.
    Composition is intentional: referencing does not support subclassing.
    """

    def __init__(self, resource: Resource[Any], validator: Any, dialects: dict[int, Any]) -> None:
        self.resource = resource
        self.validator = validator
        self.dialects = dialects
        self.dialects[id(resource.contents)] = validator

    def __getattr__(self, name: str) -> Any:
        return getattr(self.resource, name)

    def pointer(self, pointer: str, resolver: Any) -> Any:
        resource = self.resource
        if self.validator is jsonschema.Draft3Validator:
            specification = specification_with("http://json-schema.org/draft-03/schema#")

            def enter(segments: Any, resolver: Any, subresource: Any) -> Any:
                # Resource.pointer retains the entire path while schemas lack IDs.
                # Normalize every schema-position prefix, not only the final one.
                normalized = []
                index = 0
                while index < len(segments):
                    keyword = segments[index]
                    following = segments[index + 1] if index + 1 < len(segments) else None
                    if keyword in {"type", "disallow"} and isinstance(following, int):
                        index += 2
                    elif keyword == "extends" and not isinstance(following, int):
                        index += 1
                    elif keyword in {"properties", "patternProperties", "definitions", "dependencies"}:
                        normalized.extend(segments[index:index + 2])
                        index += 2
                    elif keyword in {"items", "extends"} and isinstance(following, int):
                        normalized.extend(segments[index:index + 2])
                        index += 2
                    elif keyword in {"items", "additionalItems", "additionalProperties"}:
                        normalized.append(keyword)
                        index += 1
                    else:
                        normalized.extend(segments[index:])
                        break
                return specification.maybe_in_subresource(normalized, resolver, subresource)

            pointer_specification = Specification(  # type: ignore[call-arg, unused-ignore]
                name=specification.name,
                id_of=specification.id_of,
                subresources_of=specification.subresources_of,
                maybe_in_subresource=enter,
                anchors_in=lambda _spec, contents: specification.anchors_in(contents),
            )
            resource = pointer_specification.create_resource(resource.contents)
        return _resolved_with_dialect(
            resource.pointer(pointer, resolver), self.validator, self.dialects
        )

    def anchors(self) -> Iterable[Any]:
        for anchor in self.resource.anchors():
            # DynamicAnchor.resolve checks anchor types when walking dynamic scope.
            # Preserve its identity so dynamic references can rebind correctly.
            if isinstance(anchor, DynamicAnchor):
                yield anchor
            else:
                yield _SchemaAnchor(anchor, self.validator, self.dialects)

    def subresources(self) -> Iterable[Resource[Any]]:
        import jsonschema.validators

        contents = self.resource.contents
        validator = jsonschema.validators.validator_for(contents, default=self.validator)  # type: ignore[no-untyped-call, unused-ignore]
        specification = specification_with(
            validator.META_SCHEMA.get("$id") or validator.META_SCHEMA["id"]
        )
        for contents in _schema_children(contents, validator):
            child_validator = validator
            if isinstance(contents, dict) and "$schema" in contents:
                if not isinstance(contents["$schema"], str):
                    raise InvalidSchemaError("$schema must be a string")
                child_validator = jsonschema.validators.validator_for(contents, default=None)  # type: ignore[arg-type, no-untyped-call, unused-ignore]
                if child_validator is None:
                    raise InvalidSchemaError(f"Unsupported $schema: {contents['$schema']}")
            _check_schema_node(contents, child_validator)
            child = Resource.from_contents(contents, default_specification=specification)
            yield cast(Resource[Any], _SchemaResource(child, child_validator, self.dialects))


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
    dialects: dict[int, Any] = {}
    resource = cast(Resource[Any], _SchemaResource(
        specification.create_resource(new_schema), validator_cls, dialects
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
                        try:
                            target_root = resolved.resolver.lookup("").contents
                        except Unresolvable:
                            target_root = resolved.contents
                        target_validator = jsonschema.validators.validator_for(target_root, default=dialects.get(id(resolved.contents), dialects.get(id(target_root), current_validator)))  # type: ignore[no-untyped-call, unused-ignore]
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
                for child in cast(Resource[Any], _SchemaResource(current_resource, current_validator, dialects)).subresources()
            )
        registry = registry.with_resource(resource.id() or "", resource).crawl()
    except (Unresolvable, UnknownDialect, ValueError, jsonschema.exceptions.SchemaError) as exc:
        raise InvalidSchemaError(str(exc)) from exc
    # Keep our root adapter: the default constructor replaces it with a plain Resource.
    return validator_cls(new_schema, registry=registry, _resolver=_SchemaResolver(registry.resolver_with_root(resource), validator_cls, dialects))  # type: ignore[call-arg, unused-ignore]
