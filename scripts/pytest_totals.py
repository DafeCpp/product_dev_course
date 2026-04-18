#!/usr/bin/env python3

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Totals:
    passed: int = 0
    failed: int = 0
    errors: int = 0

    def add(self, other: "Totals") -> "Totals":
        return Totals(
            passed=self.passed + other.passed,
            failed=self.failed + other.failed,
            errors=self.errors + other.errors,
        )


_RE_PASSED = re.compile(r"(?P<n>\d+)\s+passed\b")
_RE_FAILED = re.compile(r"(?P<n>\d+)\s+failed\b")
_RE_ERROR = re.compile(r"(?P<n>\d+)\s+error(s)?\b")
_RE_PYTEST_SUMMARY_LINE = re.compile(r"^=+.+\bin\s+\d+(\.\d+)?s\b.+=+\s*$")


def _extract_line_totals(line: str) -> Totals:
    passed = sum(int(m.group("n")) for m in _RE_PASSED.finditer(line))
    failed = sum(int(m.group("n")) for m in _RE_FAILED.finditer(line))
    errors = sum(int(m.group("n")) for m in _RE_ERROR.finditer(line))
    return Totals(passed=passed, failed=failed, errors=errors)


def _extract_file_totals(text: str) -> Totals:
    total = Totals()
    for line in text.splitlines():
        # Only count the final pytest summary line, e.g.:
        # "==== 1 failed, 126 passed in 37.21s ===="
        if not _RE_PYTEST_SUMMARY_LINE.match(line):
            continue
        total = total.add(_extract_line_totals(line))
    return total


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: pytest_totals.py <pytest-output-log>", file=sys.stderr)
        return 2

    path = Path(argv[1])
    if not path.exists():
        return 0

    totals = _extract_file_totals(path.read_text(errors="replace"))
    failed_total = totals.failed + totals.errors

    print()
    print(f"=== Backend test totals: {totals.passed} passed, {failed_total} failed ===")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

