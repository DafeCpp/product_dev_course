#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/dev-common.sh"

node_ready() {
    command -v "$NODE" >/dev/null 2>&1 &&
        "$NODE" -e 'process.exit(parseInt(process.versions.node, 10) >= 24 ? 0 : 1)' >/dev/null 2>&1
}

in_docker() {
    echo "Using $FRONTEND_NODE_IMAGE for npm (local Node.js 24+ is unavailable)."
    docker run --rm -v "$REPO_ROOT:/repo" -w /repo "$FRONTEND_NODE_IMAGE" sh -lc "$1"
}

case "${1:-}" in
    frontend-install) command='npm ci --no-audit --no-fund --loglevel=error' ;;
    test-frontend) command='npm run test' ;;
    test-coverage-frontend) command='npm run test:coverage' ;;
    test-frontend-docker)
        in_docker 'npm ci --no-audit --no-fund --loglevel=error && npm run test'
        exit
        ;;
    *) echo "Unknown frontend command: ${1:-}" >&2; exit 2 ;;
esac
if node_ready; then
    # Commands above are fixed strings, never supplied by callers.
    case "$1" in
        frontend-install) npm ci --no-audit --no-fund --loglevel=error ;;
        test-frontend) npm run test ;;
        test-coverage-frontend) npm run test:coverage ;;
    esac
elif command -v docker >/dev/null 2>&1; then
    if [[ "$1" != frontend-install ]]; then
        command="npm ci --no-audit --no-fund --loglevel=error && $command"
    fi
    in_docker "$command"
else
    echo 'Node.js 24+ or Docker is required for frontend commands.' >&2
    exit 1
fi
