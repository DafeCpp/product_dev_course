#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/dev-common.sh"

mode=${1:?Usage: backend.sh install|type-check|test|coverage|telemetry-cli}
resolve_python
if [[ "$mode" == telemetry-cli ]]; then
    cd projects/telemetry_cli
    if [[ ! -x .venv/bin/python ]]; then
        "$PYTHON_BIN" -m venv .venv
        .venv/bin/python -m pip install -U pip
        .venv/bin/python -m pip install -e .
    fi
    PYTHONPATH=src .venv/bin/python -m unittest discover -s tests -p 'test_*.py' -q
    exit
fi

read -r -a services <<< "${BACKEND_SERVICES:-}"
read -r -a projects <<< "${BACKEND_PROJECTS:-}"
common_dir=${BACKEND_COMMON_DIR:-projects/backend/common}
if [[ "$mode" == install ]]; then
    ((${#projects[@]} > 0)) || { echo 'No backend projects found.' >&2; exit 1; }
    for project in "${projects[@]}"; do
        echo "Installing dependencies for ${project##*/}..."
        (cd -- "$project" && poetry env use "$PYTHON_BIN" >/dev/null && poetry install --with dev)
    done
    exit
fi
((${#services[@]} > 0)) || { echo 'No backend services found.' >&2; exit 1; }
case "$mode" in
    type-check)
        failed=0
        for service in "${services[@]}"; do
            echo "Type checking ${service##*/}..."
            (cd -- "$service" && poetry run mypy src) || failed=1
        done
        exit "$failed"
        ;;
    test|coverage) prepare_test_postgres "$mode" ;;
    *) echo "Unknown backend command: $mode" >&2; exit 2 ;;
esac

failed=0
if [[ "$mode" == test ]]; then
    out_file=$(mktemp -t backend-pytest.XXXXXX.log)
    trap 'rm -f "$out_file"' EXIT
    echo 'Running backend-common tests...'
    (cd -- "$common_dir" && TEST_POSTGRESQL_DSN="$PG_TEST_DSN" poetry run pytest) 2>&1 |
        tee -a "$out_file" || failed=1
fi
for service in "${services[@]}"; do
    echo "Running $mode for ${service##*/}..."
    if [[ "$mode" == test ]]; then
        (cd -- "$service" && poetry run pytest --postgresql "$PG_TEST_DSN") 2>&1 |
            tee -a "$out_file" || failed=1
    else
        (cd -- "$service" && poetry run pytest --postgresql "$PG_TEST_DSN" \
            --cov --cov-report=term-missing --cov-report=html --cov-report=xml) || failed=1
    fi
done
if [[ "$mode" == test ]]; then
    "$PYTHON_BIN" scripts/pytest_totals.py "$out_file" || true
fi
exit "$failed"
