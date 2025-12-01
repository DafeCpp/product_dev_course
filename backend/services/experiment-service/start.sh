#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

if command -v poetry >/dev/null 2>&1; then
  exec poetry run python -m experiment_service.main
else
  exec python -m experiment_service.main
fi

