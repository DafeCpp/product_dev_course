#!/usr/bin/env bash

set -euo pipefail

if ! command -v pre-commit >/dev/null 2>&1; then
  echo "pre-commit is not installed. Install it with: python3 -m pip install pre-commit" >&2
  exit 1
fi

if ! command -v clang-format-20 >/dev/null 2>&1; then
  echo "clang-format-20 is not installed or is not in PATH." >&2
  exit 1
fi

repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root"

pre-commit install --hook-type pre-commit

echo "Installed pre-commit hook for RC Vehicle firmware clang-format."
