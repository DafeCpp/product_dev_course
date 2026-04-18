"""Typed Pydantic schemas for conversion profile payloads.

These models mirror the runtime validation in ``backend_common.conversion``
but expose a strongly-typed discriminated union that can be used for
static analysis, documentation generation, and explicit payload construction
in tests and tooling.

Payload field names must stay in sync with ``backend_common.conversion``:
- linear:       {"a": float, "b": float}
- polynomial:   {"coefficients": [float, ...]}
- lookup_table: {"table": [{"raw": float, "physical": float}, ...]}
"""
from __future__ import annotations

from typing import Annotated, Literal, Union

from pydantic import BaseModel, ConfigDict, Field


class LinearPayload(BaseModel):
    """Applies ``physical = a * raw + b``."""

    model_config = ConfigDict(extra="forbid")

    type: Literal["linear"] = "linear"
    a: float
    b: float = 0.0


class PolynomialPayload(BaseModel):
    """Applies ``physical = c[0] + c[1]*x + c[2]*x² + ...``."""

    model_config = ConfigDict(extra="forbid")

    type: Literal["polynomial"] = "polynomial"
    coefficients: list[float] = Field(min_length=1, max_length=20)


class _LookupPoint(BaseModel):
    model_config = ConfigDict(extra="forbid")

    raw: float
    physical: float


class LookupTablePayload(BaseModel):
    """Linear interpolation between calibration points; clamps beyond boundaries."""

    model_config = ConfigDict(extra="forbid")

    type: Literal["lookup_table"] = "lookup_table"
    table: list[_LookupPoint] = Field(min_length=2, max_length=1000)


# Discriminated union keyed on the ``type`` field.
# NOTE: The ``type`` field is *not* stored in the DB payload — it is carried
#       by the separate ``kind`` column on ``conversion_profiles``.  These
#       schemas are used for in-process validation and serialisation only.
ConversionPayload = Annotated[
    Union[LinearPayload, PolynomialPayload, LookupTablePayload],
    Field(discriminator="type"),
]
