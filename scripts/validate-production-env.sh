#!/usr/bin/env bash
# Validate the production dotenv file without sourcing it or printing secrets.
# The source of truth for required keys is docker-compose.prod.yml:
# expressions without a `:-default` are mandatory.
set -euo pipefail

ENV_FILE="${1:-.env}"
COMPOSE_FILE="${2:-docker-compose.prod.yml}"

if [[ ! -f "$ENV_FILE" ]]; then
  echo "ERROR: production env file is missing: $ENV_FILE" >&2
  exit 1
fi

if [[ ! -f "$COMPOSE_FILE" ]]; then
  echo "ERROR: production compose file is missing: $COMPOSE_FILE" >&2
  exit 1
fi

extract_required_keys() {
  local expression

  while IFS= read -r expression; do
    expression="${expression:2:${#expression}-3}"
    [[ "$expression" == *":-"* ]] && continue
    printf '%s\n' "${expression%%:*}"
  done < <(grep -oE '\$\{[A-Z_][A-Z0-9_]*(:[^}]*)?\}' "$COMPOSE_FILE" || true)
}

mapfile -t required_keys < <(extract_required_keys | LC_ALL=C sort -u)

if [[ ${#required_keys[@]} -eq 0 ]]; then
  echo "ERROR: no required variables found in $COMPOSE_FILE" >&2
  exit 1
fi

declare -A env_values=()

while IFS= read -r line || [[ -n "$line" ]]; do
  line="${line%$'\r'}"
  [[ "$line" =~ ^[[:space:]]*$ ]] && continue
  [[ "$line" =~ ^[[:space:]]*# ]] && continue

  if [[ "$line" =~ ^[[:space:]]*(export[[:space:]]+)?([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*=(.*)$ ]]; then
    key="${BASH_REMATCH[2]}"
    value="${BASH_REMATCH[3]}"
    value="${value#"${value%%[![:space:]]*}"}"
    value="${value%"${value##*[![:space:]]}"}"

    if [[ "$value" =~ ^\"(.*)\"$ || "$value" =~ ^\'(.*)\'$ ]]; then
      value="${BASH_REMATCH[1]}"
    else
      value="${value%%[[:space:]]#*}"
      value="${value%"${value##*[![:space:]]}"}"
    fi

    env_values["$key"]="$value"
  fi
done < "$ENV_FILE"

is_placeholder() {
  local value="$1"

  case "$value" in
    *PASSWORD*|*CHANGE_ME*|*GENERATE_*|*YOUR_*|*c-CLUSTER*|*your-domain.com*)
      return 0
      ;;
  esac

  return 1
}

errors=0
for key in "${required_keys[@]}"; do
  if [[ ! -v "env_values[$key]" ]]; then
    echo "ERROR: required variable $key is missing from $ENV_FILE" >&2
    errors=1
    continue
  fi

  value="${env_values[$key]}"
  if [[ -z "${value//[[:space:]]/}" ]]; then
    echo "ERROR: required variable $key is empty in $ENV_FILE" >&2
    errors=1
  elif is_placeholder "$value"; then
    echo "ERROR: required variable $key still contains a template value in $ENV_FILE" >&2
    errors=1
  fi
done

if [[ $errors -ne 0 ]]; then
  echo "Production env pre-flight failed; the running stack was not changed." >&2
  exit 1
fi

echo "Production env pre-flight passed (${#required_keys[@]} required variables)."
