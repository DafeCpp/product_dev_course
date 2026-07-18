#!/usr/bin/env python3
"""Validate independent line and branch coverage ratchets from JSON summaries."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
from typing import Any


def percentage(numerator: int | float, denominator: int | float) -> float:
    return 100.0 if denominator == 0 else 100.0 * numerator / denominator


def read_python_report(report: dict[str, Any]) -> tuple[float, float]:
    totals = report["totals"]
    return (
        percentage(totals["covered_lines"], totals["num_statements"]),
        percentage(totals["covered_branches"], totals["num_branches"]),
    )


def read_istanbul_report(report: dict[str, Any]) -> tuple[float, float]:
    total = report["total"]
    return float(total["lines"]["pct"]), float(total["branches"]["pct"])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--component", required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--format", choices=("python", "istanbul"), required=True)
    parser.add_argument("--min-lines", type=float, required=True)
    parser.add_argument("--min-branches", type=float, required=True)
    parser.add_argument("--enforce", choices=("true", "false"), default="true")
    args = parser.parse_args()

    report = json.loads(args.report.read_text())
    lines, branches = (
        read_python_report(report)
        if args.format == "python"
        else read_istanbul_report(report)
    )
    summary = (
        f"| {args.component} | {lines:.2f}% | {args.min_lines:.2f}% | "
        f"{branches:.2f}% | {args.min_branches:.2f}% |"
    )
    print(summary)

    if "GITHUB_STEP_SUMMARY" in os.environ:
        Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a").write(
            "## Coverage ratchet\n\n"
            "| Component | Lines | Minimum | Branches | Minimum |\n"
            "| --- | ---: | ---: | ---: | ---: |\n"
            f"{summary}\n"
        )

    failures = []
    if lines < args.min_lines:
        failures.append(f"lines {lines:.2f}% < {args.min_lines:.2f}%")
    if branches < args.min_branches:
        failures.append(f"branches {branches:.2f}% < {args.min_branches:.2f}%")
    if failures and args.enforce == "true":
        raise SystemExit(f"{args.component} coverage ratchet failed: {', '.join(failures)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
