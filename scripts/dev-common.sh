#!/usr/bin/env bash
# Shared helpers for the root Makefile. Source this file from command scripts.
REPO_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd -- "$REPO_ROOT"

resolve_python() {
    local candidate
    for candidate in "${PYTHON:-python3.14}" "${HOME}/.pyenv/shims/python3.14" python3.14 python3 python; do
        if [[ -n "$candidate" ]] && command -v "$candidate" >/dev/null 2>&1; then
            PYTHON_BIN=$(command -v "$candidate")
            # Resolve relative executable paths before changing project directories.
            if [[ "$PYTHON_BIN" != /* ]]; then
                PYTHON_BIN="$REPO_ROOT/$PYTHON_BIN"
            fi
            if ! "$PYTHON_BIN" -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 14) else 1)'; then
                echo "Python 3.14+ is required: $candidate. Set PYTHON=/path/to/python3.14." >&2
                return 1
            fi
            return 0
        fi
    done
    echo 'Python not found. Set PYTHON=/path/to/python3.14.' >&2
    return 1
}

init_compose() {
    if [[ -n "${DOCKER_COMPOSE:-}" ]]; then
        read -r -a COMPOSE <<< "$DOCKER_COMPOSE"
    elif docker compose version >/dev/null 2>&1; then
        COMPOSE=(docker compose)
    else
        COMPOSE=(docker-compose)
    fi
}

prepare_test_postgres() {
    PG_TEST_DSN=${TEST_POSTGRESQL_DSN:-}
    [[ -z "$PG_TEST_DSN" ]] || return 0
    init_compose
    echo 'Starting TimescaleDB for backend tests...' >&2
    if ! "${COMPOSE[@]}" up -d postgres >/dev/null; then
        echo 'Cannot start PostgreSQL. Check Docker access or set TEST_POSTGRESQL_DSN.' >&2
        return 1
    fi
    local ready=0 attempt hostport pg_user pg_pass
    for ((attempt = 0; attempt < 60; attempt++)); do
        if "${COMPOSE[@]}" exec -T postgres pg_isready -U postgres -d postgres >/dev/null 2>&1; then
            ready=1
            break
        fi
        sleep 0.5
    done
    if ((ready == 0)); then
        echo 'PostgreSQL is not ready.' >&2
        "${COMPOSE[@]}" logs --tail=80 postgres >&2 || true
        return 1
    fi
    hostport=$("${COMPOSE[@]}" port postgres 5432 | tail -n 1)
    hostport=${hostport##*:}
    if [[ ! "$hostport" =~ ^[0-9]+$ ]] || ((10#$hostport < 1 || 10#$hostport > 65535)); then
        echo 'Cannot discover the PostgreSQL host port.' >&2
        return 1
    fi
    pg_user=$("${COMPOSE[@]}" exec -T postgres sh -c 'printf "%s" "${POSTGRES_USER:-postgres}"')
    pg_pass=$("${COMPOSE[@]}" exec -T postgres sh -c 'printf "%s" "${POSTGRES_PASSWORD:-postgres}"')
    # Retain test-backend's dev-password refresh; coverage only reads credentials.
    if [[ "${1:-}" == test ]]; then
        printf 'ALTER USER :"role" WITH PASSWORD :\047password\047;\n' |
            "${COMPOSE[@]}" exec -T postgres psql -U postgres -d postgres \
                -v "role=$pg_user" -v "password=$pg_pass" >/dev/null 2>&1 || true
    fi
    # DSNs must remain valid when local credentials contain URI delimiters.
    PG_TEST_DSN=$("$PYTHON_BIN" -c 'import sys; from urllib.parse import quote; print("postgresql://{}:{}@localhost:{}/postgres".format(quote(sys.argv[1], safe=""), quote(sys.argv[2], safe=""), sys.argv[3]))' "$pg_user" "$pg_pass" "$hostport")
}
