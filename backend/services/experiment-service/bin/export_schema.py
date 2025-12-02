#!/usr/bin/env python3
"""Export test schema SQL by concatenating migrations."""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import textwrap
from typing import List, Sequence, Tuple


def default_migrations_dir() -> Path:
    return Path(__file__).resolve().parent.parent / "migrations"


def default_output_path() -> Path:
    return (
        Path(__file__)
        .resolve()
        .parent.parent
        / "tests"
        / "schemas"
        / "postgresql"
        / "experiment_service.sql"
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate test schema SQL from migration files."
    )
    parser.add_argument(
        "--migrations-dir",
        "-m",
        type=Path,
        default=default_migrations_dir(),
        help="Directory with *.sql migrations.",
    )
    parser.add_argument(
        "--output",
        "-o",
        type=Path,
        default=default_output_path(),
        help="Target SQL file (testsuite schema).",
    )
    return parser.parse_args()


CREATE_TABLE_RE = re.compile(
    r"CREATE\s+TABLE\s+(?:IF\s+NOT\s+EXISTS\s+)?([a-zA-Z0-9_\.]+)", re.IGNORECASE
)
CREATE_TYPE_RE = re.compile(
    r"CREATE\s+TYPE\s+(?:IF\s+NOT\s+EXISTS\s+)?([a-zA-Z0-9_\.]+)", re.IGNORECASE
)
CREATE_FUNCTION_RE = re.compile(
    r"CREATE\s+OR\s+REPLACE\s+FUNCTION\s+([a-zA-Z0-9_\.]+)\s*\(([^)]*)\)",
    re.IGNORECASE | re.DOTALL,
)


def strip_wrappers(sql: str) -> str:
    lines = [line.rstrip() for line in sql.splitlines()]
    # remove first BEGIN, even if preceded by comments
    for idx, line in enumerate(lines):
        if line.strip().upper() in {"BEGIN;", "BEGIN"}:
            lines.pop(idx)
            break
    # remove last COMMIT, even if trailing whitespace
    for idx in range(len(lines) - 1, -1, -1):
        if lines[idx].strip().upper() in {"COMMIT;", "COMMIT"}:
            lines.pop(idx)
            break
    return "\n".join(lines).strip()


def collect_objects(sql: str) -> Tuple[List[str], List[str], List[Tuple[str, str]]]:
    tables: List[str] = []
    types: List[str] = []
    functions: List[Tuple[str, str]] = []

    for match in CREATE_TABLE_RE.finditer(sql):
        name = match.group(1)
        if name not in tables:
            tables.append(name)

    for match in CREATE_TYPE_RE.finditer(sql):
        name = match.group(1)
        if name not in types:
            types.append(name)

    for match in CREATE_FUNCTION_RE.finditer(sql):
        func = match.group(1)
        params = match.group(2).strip()
        signature = func
        if f"{func}({params})" not in (f"{f}({p})" for f, p in functions):
            functions.append((func, params))
    return tables, types, functions


def build_drop_section(
    tables: Sequence[str], types: Sequence[str], functions: Sequence[Tuple[str, str]]
) -> str:
    lines: List[str] = []
    for table in reversed(tables):
        lines.append(f"DROP TABLE IF EXISTS {table} CASCADE;")
    for type_name in reversed(types):
        lines.append(f"DROP TYPE IF EXISTS {type_name} CASCADE;")
    for func_name, params in functions:
        param_sig = params.replace("\n", " ").strip()
        lines.append(
            f"DROP FUNCTION IF EXISTS {func_name}({param_sig}) CASCADE;"
            if param_sig
            else f"DROP FUNCTION IF EXISTS {func_name}() CASCADE;"
        )
    return "\n".join(lines)


def main() -> None:
    args = parse_args()
    migrations = sorted(args.migrations_dir.glob("*.sql"))
    if not migrations:
        raise SystemExit(f"No migrations found in {args.migrations_dir}")

    all_tables: List[str] = []
    all_types: List[str] = []
    all_functions: List[Tuple[str, str]] = []
    body_parts: List[str] = []

    for path in migrations:
        sql = path.read_text(encoding="utf-8")
        tables, types, functions = collect_objects(sql)
        for name in tables:
            if name not in all_tables:
                all_tables.append(name)
        for name in types:
            if name not in all_types:
                all_types.append(name)
        for func_sig in functions:
            if func_sig not in all_functions:
                all_functions.append(func_sig)

        body_parts.append(
            f"-- Migration: {path.name}\n{strip_wrappers(sql)}\n"
        )

    header = textwrap.dedent(
        """\
        -- Auto-generated from migrations.
        -- Run `poetry run python bin/export_schema.py` after editing migrations.
        """
    )

    drop_section = build_drop_section(all_tables, all_types, all_functions)
    content = "\n".join(
        [
            header,
            "BEGIN;",
            drop_section,
            "",
            "\n".join(body_parts).strip(),
            "COMMIT;",
            "",
        ]
    )
    args.output.write_text(content, encoding="utf-8")
    print(f"Wrote schema to {args.output}")


if __name__ == "__main__":
    main()

