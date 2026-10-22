#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/dev-common.sh"
if ! command -v cmake >/dev/null 2>&1; then
    echo 'cmake is required for firmware coverage.' >&2
    exit 1
fi
cd projects/rc_vehicle/firmware/tests
cmake -B build-coverage -DENABLE_COVERAGE=ON
cmake --build build-coverage -j
if command -v lcov >/dev/null 2>&1 && command -v genhtml >/dev/null 2>&1; then
    cmake --build build-coverage --target coverage
else
    echo 'lcov/genhtml not found; running host tests without an HTML coverage report.'
    (cd build-coverage && ctest --output-on-failure)
fi
