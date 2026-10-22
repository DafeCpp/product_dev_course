#!/usr/bin/env bash
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/dev-common.sh"
init_compose
read -r -a all_services <<< "$DEV_ALL_SERVICES"
read -r -a backend_services <<< "$BACKEND_DEV_SERVICES"
read -r -a make_command <<< "${MAKE:-make}"

prepare_dev_files() {
    if [[ ! -f docker-compose.override.yml ]]; then
        cp docker-compose.override.yml.example docker-compose.override.yml
    fi
    if [[ ! -f .env ]]; then
        cp env.docker.example .env
    fi
}

# Use BuildKit when available and share the same fallback in every build target.
build_image() {
    if docker buildx version >/dev/null 2>&1; then
        "$@"
    else
        DOCKER_BUILDKIT=0 COMPOSE_DOCKER_CLI_BUILD=0 "$@"
    fi
}

backend_base_check() {
    local current_hash
    current_hash=$(docker image inspect backend-base:local \
        --format '{{ index .Config.Labels "org.opencontainers.image.base.dockerfile-hash" }}' 2>/dev/null) || return 1
    [[ -n "$current_hash" && "$current_hash" == "$BACKEND_BASE_HASH" ]]
}

backend_base() {
    if [[ "${FORCE_BACKEND_BASE:-}" != 1 && "${1:-}" != --no-cache ]] && backend_base_check; then
        echo 'Backend base image is up to date.'
        return
    fi
    build_image docker build "$@" -f "$BACKEND_BASE_DOCKERFILE" \
        --build-arg "BASE_IMAGE_HASH=$BACKEND_BASE_HASH" -t backend-base:local projects/backend
}

dev_up() {
    backend_base
    prepare_dev_files
    "${COMPOSE[@]}" up -d "${all_services[@]}"
    "${make_command[@]}" dev-seed
    echo 'Development stack started: portal :3000, auth :8001, experiments :8002, telemetry :8003.'
    echo 'Auth proxy :8080, sensor simulator :8082, Grafana :3001.'
    echo 'Use make dev-logs to follow logs, make help for other commands.'
}

dev_rebuild_changed() {
    local changed_files base_changed=0 path service
    local services=()
    changed_files=$(git status --porcelain=v1 -uall | awk '{print $2}')
    while IFS= read -r path; do
        case "$path" in
            projects/backend/Dockerfile.base)
                base_changed=1
                services+=("${backend_services[@]}")
                ;;
            projects/backend/common/*) services+=("${backend_services[@]}") ;;
            projects/backend/services/*)
                for service in "${backend_services[@]}"; do
                    [[ "$path" != projects/backend/services/"$service"/* ]] || services+=("$service")
                done
                ;;
            package.json|package-lock.json|projects/frontend/common/*|projects/frontend/tsconfig*.json)
                services+=(auth-proxy experiment-portal sensor-simulator)
                ;;
            projects/frontend/apps/*)
                for service in auth-proxy experiment-portal sensor-simulator; do
                    [[ "$path" != projects/frontend/apps/"$service"/* ]] || services+=("$service")
                done
                ;;
        esac
    done <<< "$changed_files"
    if ((${#services[@]} == 0)); then
        echo 'No image rebuild needed for local changes.'
        return
    fi
    mapfile -t services <<< "$(printf '%s\n' "${services[@]}" | sort -u)"
    for service in "${services[@]}"; do
        if [[ " ${backend_services[*]} " == *" $service "* ]]; then
            if ((base_changed)); then backend_base --no-cache; else backend_base; fi
            break
        fi
    done
    build_image "${COMPOSE[@]}" build "${services[@]}"
    "${COMPOSE[@]}" up -d "${services[@]}"
}

dev_service() {
    local service=${1:?Service is required}
    if [[ " ${backend_services[*]} " != *" $service "* ]]; then
        echo "Unknown backend service: $service. Available: ${backend_services[*]}" >&2
        return 1
    fi
    backend_base
    prepare_dev_files
    build_image "${COMPOSE[@]}" build "$service"
    "${COMPOSE[@]}" up -d "$service"
}

dev_clean() {
    echo 'WARNING: deleting development containers and volumes.'
    "${COMPOSE[@]}" down -v --remove-orphans "$@"
    # Also remove the separately launched logging project, if present.
    (cd infrastructure/logging && "${COMPOSE[@]}" -f docker-compose.yml down -v --remove-orphans "$@")
}

case "${1:-}" in
    dev-up) dev_up ;;
    dev-rebuild)
        backend_base --no-cache
        build_image "${COMPOSE[@]}" build --no-cache "${all_services[@]}"
        dev_up
        ;;
    dev-rebuild-changed) dev_rebuild_changed ;;
    dev-service) dev_service "${2:?Service is required}" ;;
    backend-base) backend_base ;;
    backend-base-no-cache) FORCE_BACKEND_BASE=1 backend_base --no-cache ;;
    backend-base-check)
        if backend_base_check; then
            echo 'Backend base image is up to date.'
        else
            echo 'Backend base image is missing or outdated. Run make backend-base.' >&2
            exit 1
        fi
        ;;
    dev-fix)
        echo 'Recreating development containers (persistent data is retained).'
        "${COMPOSE[@]}" down --remove-orphans
        dev_up
        ;;
    dev-clean) dev_clean ;;
    dev-clean-all) dev_clean --rmi local ;;
    *) echo "Unknown dev command: ${1:-}" >&2; exit 2 ;;
esac
