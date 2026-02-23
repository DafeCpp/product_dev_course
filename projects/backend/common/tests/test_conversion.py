"""Unit tests for backend_common.conversion module."""
from __future__ import annotations

import pytest

from backend_common.conversion import apply_conversion


# ---------------------------------------------------------------------------
# Linear
# ---------------------------------------------------------------------------
class TestLinear:
    def test_basic(self):
        assert apply_conversion("linear", {"a": 2.0, "b": 1.0}, 3.0) == 7.0

    def test_zero(self):
        assert apply_conversion("linear", {"a": 1.0, "b": 0.0}, 0.0) == 0.0

    def test_negative(self):
        assert apply_conversion("linear", {"a": -1.0, "b": 10.0}, 3.0) == 7.0

    def test_integer_coefficients(self):
        assert apply_conversion("linear", {"a": 2, "b": 3}, 5) == 13.0

    def test_missing_a(self):
        assert apply_conversion("linear", {"b": 1.0}, 3.0) is None

    def test_missing_b(self):
        assert apply_conversion("linear", {"a": 1.0}, 3.0) is None

    def test_string_coefficient(self):
        assert apply_conversion("linear", {"a": "two", "b": 1.0}, 3.0) is None

    def test_empty_payload(self):
        assert apply_conversion("linear", {}, 3.0) is None


# ---------------------------------------------------------------------------
# Polynomial
# ---------------------------------------------------------------------------
class TestPolynomial:
    def test_constant(self):
        # c0 = 5 → always 5
        assert apply_conversion("polynomial", {"coefficients": [5.0]}, 100.0) == 5.0

    def test_linear_equivalent(self):
        # c0 + c1*x = 1 + 2*3 = 7
        assert apply_conversion("polynomial", {"coefficients": [1.0, 2.0]}, 3.0) == 7.0

    def test_quadratic(self):
        # c0 + c1*x + c2*x² = 1 + 0*x + 2*x² = 1 + 2*9 = 19
        assert apply_conversion("polynomial", {"coefficients": [1.0, 0.0, 2.0]}, 3.0) == 19.0

    def test_cubic(self):
        # 0 + 0*x + 0*x² + 1*x³ = 8
        assert apply_conversion("polynomial", {"coefficients": [0, 0, 0, 1]}, 2.0) == 8.0

    def test_empty_coefficients(self):
        assert apply_conversion("polynomial", {"coefficients": []}, 3.0) is None

    def test_missing_coefficients(self):
        assert apply_conversion("polynomial", {}, 3.0) is None

    def test_non_numeric_coefficient(self):
        assert apply_conversion("polynomial", {"coefficients": [1.0, "bad"]}, 3.0) is None

    def test_zero_input(self):
        # c0 + c1*0 + c2*0 = c0 = 42
        assert apply_conversion("polynomial", {"coefficients": [42.0, 7.0, 3.0]}, 0.0) == 42.0


# ---------------------------------------------------------------------------
# Lookup table
# ---------------------------------------------------------------------------
class TestLookupTable:
    SIMPLE_TABLE = [
        {"raw": 0.0, "physical": 0.0},
        {"raw": 10.0, "physical": 100.0},
        {"raw": 20.0, "physical": 200.0},
    ]

    def test_exact_point(self):
        assert apply_conversion("lookup_table", {"table": self.SIMPLE_TABLE}, 10.0) == 100.0

    def test_interpolation_midpoint(self):
        assert apply_conversion("lookup_table", {"table": self.SIMPLE_TABLE}, 5.0) == 50.0

    def test_interpolation_quarter(self):
        assert apply_conversion("lookup_table", {"table": self.SIMPLE_TABLE}, 2.5) == 25.0

    def test_clamp_below(self):
        assert apply_conversion("lookup_table", {"table": self.SIMPLE_TABLE}, -5.0) == 0.0

    def test_clamp_above(self):
        assert apply_conversion("lookup_table", {"table": self.SIMPLE_TABLE}, 30.0) == 200.0

    def test_boundary_low(self):
        assert apply_conversion("lookup_table", {"table": self.SIMPLE_TABLE}, 0.0) == 0.0

    def test_boundary_high(self):
        assert apply_conversion("lookup_table", {"table": self.SIMPLE_TABLE}, 20.0) == 200.0

    def test_unsorted_table(self):
        # Table provided out of order should still work (sorted internally)
        unsorted = [
            {"raw": 20.0, "physical": 200.0},
            {"raw": 0.0, "physical": 0.0},
            {"raw": 10.0, "physical": 100.0},
        ]
        assert apply_conversion("lookup_table", {"table": unsorted}, 5.0) == 50.0

    def test_non_linear_table(self):
        table = [
            {"raw": 0.0, "physical": 0.0},
            {"raw": 10.0, "physical": 50.0},
            {"raw": 20.0, "physical": 200.0},
        ]
        # Between 10→50 and 20→200: at 15 → 50 + 0.5*(200-50) = 125
        assert apply_conversion("lookup_table", {"table": table}, 15.0) == 125.0

    def test_single_point(self):
        assert apply_conversion("lookup_table", {"table": [{"raw": 0.0, "physical": 0.0}]}, 5.0) is None

    def test_empty_table(self):
        assert apply_conversion("lookup_table", {"table": []}, 5.0) is None

    def test_missing_table(self):
        assert apply_conversion("lookup_table", {}, 5.0) is None

    def test_invalid_entry_format(self):
        assert apply_conversion("lookup_table", {"table": [1, 2, 3]}, 5.0) is None


# ---------------------------------------------------------------------------
# Unknown kind
# ---------------------------------------------------------------------------
class TestUnknownKind:
    def test_returns_none(self):
        assert apply_conversion("custom_formula", {"expr": "x*2"}, 5.0) is None

    def test_empty_string(self):
        assert apply_conversion("", {}, 5.0) is None
