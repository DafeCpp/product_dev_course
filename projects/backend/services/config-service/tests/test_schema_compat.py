"""Unit tests for schema compat checker — one test per additive/breaking rule."""
from __future__ import annotations

import pytest

from config_service.services.schema_service import _check_compat


BASE_SCHEMA = {
    "type": "object",
    "required": ["enabled"],
    "properties": {
        "enabled": {"type": "boolean"},
        "count": {"type": "integer", "minimum": 0, "maximum": 100},
        "mode": {"enum": ["read", "write"]},
    },
}


# ─── Additive (allowed) changes ──────────────────────────────────────────────


def test_additive_new_optional_field():
    new = {**BASE_SCHEMA, "properties": {**BASE_SCHEMA["properties"], "label": {"type": "string"}}}
    assert _check_compat(BASE_SCHEMA, new) == []


def test_additive_remove_required():
    new = {**BASE_SCHEMA, "required": []}
    assert _check_compat(BASE_SCHEMA, new) == []


def test_additive_raise_maximum():
    new_props = {**BASE_SCHEMA["properties"]}
    new_props["count"] = {"type": "integer", "minimum": 0, "maximum": 200}
    new = {**BASE_SCHEMA, "properties": new_props}
    assert _check_compat(BASE_SCHEMA, new) == []


def test_additive_lower_minimum():
    new_props = {**BASE_SCHEMA["properties"]}
    new_props["count"] = {"type": "integer", "minimum": -5, "maximum": 100}
    new = {**BASE_SCHEMA, "properties": new_props}
    assert _check_compat(BASE_SCHEMA, new) == []


def test_additive_expand_enum():
    new_props = {**BASE_SCHEMA["properties"]}
    new_props["mode"] = {"enum": ["read", "write", "admin"]}
    new = {**BASE_SCHEMA, "properties": new_props}
    assert _check_compat(BASE_SCHEMA, new) == []


def test_no_changes():
    assert _check_compat(BASE_SCHEMA, BASE_SCHEMA) == []


# ─── Breaking (forbidden) changes ────────────────────────────────────────────


def test_breaking_add_required_field():
    new = {**BASE_SCHEMA, "required": ["enabled", "count"]}
    violations = _check_compat(BASE_SCHEMA, new)
    assert any("required" in v.lower() for v in violations)


def test_breaking_lower_maximum():
    new_props = {**BASE_SCHEMA["properties"]}
    new_props["count"] = {"type": "integer", "minimum": 0, "maximum": 50}
    new = {**BASE_SCHEMA, "properties": new_props}
    violations = _check_compat(BASE_SCHEMA, new)
    assert any("maximum" in v for v in violations)


def test_breaking_raise_minimum():
    new_props = {**BASE_SCHEMA["properties"]}
    new_props["count"] = {"type": "integer", "minimum": 10, "maximum": 100}
    new = {**BASE_SCHEMA, "properties": new_props}
    violations = _check_compat(BASE_SCHEMA, new)
    assert any("minimum" in v for v in violations)


def test_breaking_remove_enum_value():
    new_props = {**BASE_SCHEMA["properties"]}
    new_props["mode"] = {"enum": ["read"]}
    new = {**BASE_SCHEMA, "properties": new_props}
    violations = _check_compat(BASE_SCHEMA, new)
    assert any("enum" in v for v in violations)


def test_breaking_remove_field():
    new_props = {k: v for k, v in BASE_SCHEMA["properties"].items() if k != "count"}
    new = {**BASE_SCHEMA, "properties": new_props}
    violations = _check_compat(BASE_SCHEMA, new)
    assert any("removed" in v.lower() for v in violations)


def test_breaking_change_type():
    new_props = {**BASE_SCHEMA["properties"]}
    new_props["enabled"] = {"type": "string"}
    new = {**BASE_SCHEMA, "properties": new_props}
    violations = _check_compat(BASE_SCHEMA, new)
    assert any("type" in v for v in violations)
