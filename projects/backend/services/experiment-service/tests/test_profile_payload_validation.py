"""Unit tests for conversion profile payload validation.

Covers:
- Pydantic discriminated union models in ``domain.profile_schemas``
- ``ConversionProfileInputDTO`` model validator (delegates to
  ``backend_common.conversion.validate_conversion_payload``)
"""
from __future__ import annotations

import pytest
from pydantic import TypeAdapter, ValidationError

from experiment_service.domain.dto import ConversionProfileInputDTO
from experiment_service.domain.profile_schemas import (
    ConversionPayload,
    LinearPayload,
    LookupTablePayload,
    PolynomialPayload,
)

_ta = TypeAdapter(ConversionPayload)


# ---------------------------------------------------------------------------
# LinearPayload
# ---------------------------------------------------------------------------


class TestLinearPayload:
    def test_valid_linear_payload(self) -> None:
        p = LinearPayload(a=2.5, b=-1.0)
        assert p.a == 2.5
        assert p.b == -1.0
        assert p.type == "linear"

    def test_linear_default_offset(self) -> None:
        p = LinearPayload(a=1.0)
        assert p.b == 0.0

    def test_linear_integer_coefficients_coerced(self) -> None:
        p = LinearPayload(a=2, b=3)
        assert isinstance(p.a, float)
        assert isinstance(p.b, float)

    def test_linear_missing_a_raises(self) -> None:
        with pytest.raises(ValidationError):
            LinearPayload(b=1.0)  # type: ignore[call-arg]

    def test_linear_extra_fields_forbidden(self) -> None:
        with pytest.raises(ValidationError):
            LinearPayload(a=1.0, b=0.0, extra_key="oops")  # type: ignore[call-arg]


# ---------------------------------------------------------------------------
# PolynomialPayload
# ---------------------------------------------------------------------------


class TestPolynomialPayload:
    def test_valid_polynomial_payload(self) -> None:
        p = PolynomialPayload(coefficients=[1.0, 2.0, 3.0])
        assert len(p.coefficients) == 3

    def test_single_coefficient(self) -> None:
        p = PolynomialPayload(coefficients=[42.0])
        assert p.coefficients == [42.0]

    def test_empty_coefficients_raises(self) -> None:
        with pytest.raises(ValidationError):
            PolynomialPayload(coefficients=[])

    def test_too_many_coefficients_raises(self) -> None:
        with pytest.raises(ValidationError):
            PolynomialPayload(coefficients=[1.0] * 21)

    def test_missing_coefficients_raises(self) -> None:
        with pytest.raises(ValidationError):
            PolynomialPayload()  # type: ignore[call-arg]


# ---------------------------------------------------------------------------
# LookupTablePayload
# ---------------------------------------------------------------------------


class TestLookupTablePayload:
    _VALID_TABLE = [
        {"raw": 0.0, "physical": 0.0},
        {"raw": 10.0, "physical": 100.0},
    ]

    def test_valid_lookup_table_payload(self) -> None:
        p = LookupTablePayload(table=self._VALID_TABLE)
        assert len(p.table) == 2
        assert p.table[0].raw == 0.0
        assert p.table[0].physical == 0.0

    def test_single_point_raises(self) -> None:
        with pytest.raises(ValidationError):
            LookupTablePayload(table=[{"raw": 0.0, "physical": 0.0}])

    def test_empty_table_raises(self) -> None:
        with pytest.raises(ValidationError):
            LookupTablePayload(table=[])

    def test_missing_raw_key_raises(self) -> None:
        with pytest.raises(ValidationError):
            LookupTablePayload(
                table=[{"physical": 0.0}, {"raw": 10.0, "physical": 100.0}]
            )

    def test_missing_physical_key_raises(self) -> None:
        with pytest.raises(ValidationError):
            LookupTablePayload(
                table=[{"raw": 0.0}, {"raw": 10.0, "physical": 100.0}]
            )

    def test_non_numeric_raw_raises(self) -> None:
        with pytest.raises(ValidationError):
            LookupTablePayload(
                table=[
                    {"raw": "zero", "physical": 0.0},
                    {"raw": 10.0, "physical": 100.0},
                ]
            )


# ---------------------------------------------------------------------------
# ConversionPayload discriminated union
# ---------------------------------------------------------------------------


class TestConversionPayloadDiscriminatedUnion:
    def test_valid_linear_payload(self) -> None:
        result = _ta.validate_python({"type": "linear", "a": 2.5, "b": -1.0})
        assert isinstance(result, LinearPayload)

    def test_valid_polynomial_payload(self) -> None:
        result = _ta.validate_python(
            {"type": "polynomial", "coefficients": [0.0, 1.0, 0.5]}
        )
        assert isinstance(result, PolynomialPayload)

    def test_valid_lookup_table_payload(self) -> None:
        result = _ta.validate_python(
            {
                "type": "lookup_table",
                "table": [
                    {"raw": 0.0, "physical": 0.0},
                    {"raw": 10.0, "physical": 100.0},
                ],
            }
        )
        assert isinstance(result, LookupTablePayload)

    def test_invalid_payload_unknown_type(self) -> None:
        with pytest.raises(ValidationError):
            _ta.validate_python({"type": "custom", "expression": "x*2"})

    def test_invalid_payload_missing_fields(self) -> None:
        with pytest.raises(ValidationError):
            _ta.validate_python({"type": "linear"})  # missing 'a'

    def test_invalid_payload_no_type_field(self) -> None:
        with pytest.raises(ValidationError):
            _ta.validate_python({"a": 1.0, "b": 0.0})


# ---------------------------------------------------------------------------
# ConversionProfileInputDTO – model validator integration
# ---------------------------------------------------------------------------


class TestConversionProfileInputDTOPayloadValidation:
    """Tests that the DTO's model validator rejects invalid payloads."""

    def _make_dto(self, kind: str, payload: dict) -> ConversionProfileInputDTO:
        return ConversionProfileInputDTO(
            version="v1",
            kind=kind,  # type: ignore[arg-type]
            payload=payload,
        )

    def test_valid_linear_payload(self) -> None:
        dto = self._make_dto("linear", {"a": 1.0, "b": 0.5})
        assert dto.payload == {"a": 1.0, "b": 0.5}

    def test_valid_polynomial_payload(self) -> None:
        dto = self._make_dto("polynomial", {"coefficients": [1.0, 2.0]})
        assert dto.payload["coefficients"] == [1.0, 2.0]

    def test_valid_lookup_table_payload(self) -> None:
        dto = self._make_dto(
            "lookup_table",
            {"table": [{"raw": 0.0, "physical": 0.0}, {"raw": 10.0, "physical": 100.0}]},
        )
        assert len(dto.payload["table"]) == 2

    def test_invalid_payload_unknown_type(self) -> None:
        """kind='custom' is not in the Literal and is rejected by Pydantic before the validator."""
        with pytest.raises(ValidationError):
            self._make_dto("custom", {"expression": "x*2"})

    def test_invalid_payload_missing_fields(self) -> None:
        """linear payload missing 'b' is rejected by backend_common validator."""
        with pytest.raises(ValidationError, match="'b'"):
            self._make_dto("linear", {"a": 2.0})

    def test_invalid_payload_linear_wrong_type(self) -> None:
        """linear payload with non-numeric 'a' is rejected."""
        with pytest.raises(ValidationError):
            self._make_dto("linear", {"a": "two", "b": 1.0})

    def test_invalid_payload_polynomial_empty_coefficients(self) -> None:
        with pytest.raises(ValidationError, match="empty"):
            self._make_dto("polynomial", {"coefficients": []})

    def test_invalid_payload_polynomial_missing_key(self) -> None:
        with pytest.raises(ValidationError, match="coefficients"):
            self._make_dto("polynomial", {"a0": 1.0})

    def test_invalid_payload_lookup_table_too_few_points(self) -> None:
        with pytest.raises(ValidationError, match="at least 2"):
            self._make_dto(
                "lookup_table",
                {"table": [{"raw": 0.0, "physical": 0.0}]},
            )

    def test_invalid_payload_lookup_table_missing_raw(self) -> None:
        with pytest.raises(ValidationError, match="'raw'"):
            self._make_dto(
                "lookup_table",
                {"table": [{"physical": 0.0}, {"raw": 10.0, "physical": 100.0}]},
            )

    def test_create_profile_with_invalid_payload_raises_validation_error(self) -> None:
        """Simulates what the route does: model_validate on raw JSON body."""
        raw_body = {
            "version": "v1",
            "kind": "linear",
            "payload": {"a": 1.0},  # missing 'b'
        }
        with pytest.raises(ValidationError):
            ConversionProfileInputDTO.model_validate(raw_body)
