"""Conversion profile application logic (shared between services).

Supports three conversion kinds:
- linear: physical = a * raw + b
- polynomial: physical = c0 + c1*x + c2*x² + ...
- lookup_table: linear interpolation between points, clamp beyond boundaries
"""
from __future__ import annotations

from typing import Any

SUPPORTED_KINDS = ("linear", "polynomial", "lookup_table")


def validate_conversion_payload(kind: str, payload: dict[str, Any]) -> None:
    """Validate that *payload* matches the required schema for *kind*.

    Raises ``ValueError`` with a human-readable message if invalid.
    """
    if kind not in SUPPORTED_KINDS:
        raise ValueError(
            f"kind must be one of {list(SUPPORTED_KINDS)!r}; got {kind!r}"
        )
    if kind == "linear":
        _validate_linear(payload)
    elif kind == "polynomial":
        _validate_polynomial(payload)
    elif kind == "lookup_table":
        _validate_lookup_table(payload)


def _validate_linear(payload: dict[str, Any]) -> None:
    for key in ("a", "b"):
        val = payload.get(key)
        if val is None:
            raise ValueError(f"linear payload must include '{key}' (float)")
        if not isinstance(val, (int, float)):
            raise ValueError(f"linear payload field '{key}' must be a number; got {type(val).__name__!r}")


def _validate_polynomial(payload: dict[str, Any]) -> None:
    coefficients = payload.get("coefficients")
    if not isinstance(coefficients, list):
        raise ValueError("polynomial payload must include 'coefficients' (list of numbers)")
    if len(coefficients) == 0:
        raise ValueError("polynomial payload 'coefficients' must not be empty")
    for i, c in enumerate(coefficients):
        if not isinstance(c, (int, float)):
            raise ValueError(
                f"polynomial payload 'coefficients[{i}]' must be a number; got {type(c).__name__!r}"
            )


def _validate_lookup_table(payload: dict[str, Any]) -> None:
    table = payload.get("table")
    if not isinstance(table, list):
        raise ValueError("lookup_table payload must include 'table' (list of {raw, physical} objects)")
    if len(table) < 2:
        raise ValueError("lookup_table payload 'table' must contain at least 2 points")
    for i, point in enumerate(table):
        if not isinstance(point, dict):
            raise ValueError(f"lookup_table payload 'table[{i}]' must be an object with 'raw' and 'physical' keys")
        for key in ("raw", "physical"):
            val = point.get(key)
            if val is None:
                raise ValueError(f"lookup_table payload 'table[{i}]' must include '{key}' (number)")
            if not isinstance(val, (int, float)):
                raise ValueError(
                    f"lookup_table payload 'table[{i}].{key}' must be a number; got {type(val).__name__!r}"
                )


def apply_conversion(kind: str, payload: dict[str, Any], raw_value: float) -> float | None:
    """Apply a conversion profile to a raw value.

    Returns the computed physical value, or ``None`` when the payload is
    invalid or the *kind* is not recognised.
    """
    if kind == "linear":
        return _apply_linear(payload, raw_value)
    if kind == "polynomial":
        return _apply_polynomial(payload, raw_value)
    if kind == "lookup_table":
        return _apply_lookup_table(payload, raw_value)
    return None


def _apply_linear(payload: dict[str, Any], raw_value: float) -> float | None:
    a_raw = payload.get("a")
    b_raw = payload.get("b")
    if not isinstance(a_raw, (int, float)) or not isinstance(b_raw, (int, float)):
        return None
    return float(a_raw) * raw_value + float(b_raw)


def _apply_polynomial(payload: dict[str, Any], raw_value: float) -> float | None:
    coefficients = payload.get("coefficients")
    if not isinstance(coefficients, list) or len(coefficients) == 0:
        return None
    result = 0.0
    power = 1.0
    for i, c in enumerate(coefficients):
        if not isinstance(c, (int, float)):
            return None
        result += float(c) * power
        power *= raw_value
    return result


def _apply_lookup_table(payload: dict[str, Any], raw_value: float) -> float | None:
    table = payload.get("table")
    if not isinstance(table, list) or len(table) < 2:
        return None
    try:
        sorted_points = sorted(table, key=lambda p: float(p["raw"]))
    except (TypeError, KeyError, ValueError):
        return None

    # Clamp to boundary values
    if raw_value <= float(sorted_points[0]["raw"]):
        return float(sorted_points[0]["physical"])
    if raw_value >= float(sorted_points[-1]["raw"]):
        return float(sorted_points[-1]["physical"])

    # Linear interpolation
    for i in range(len(sorted_points) - 1):
        x0 = float(sorted_points[i]["raw"])
        y0 = float(sorted_points[i]["physical"])
        x1 = float(sorted_points[i + 1]["raw"])
        y1 = float(sorted_points[i + 1]["physical"])
        if x0 <= raw_value <= x1:
            t = (raw_value - x0) / (x1 - x0) if x1 != x0 else 0.0
            return y0 + t * (y1 - y0)
    return None
