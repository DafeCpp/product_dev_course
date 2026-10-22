#!/usr/bin/env bash
set -euo pipefail
printf 'Usage: make <target> [VARIABLE=value ...]\n\n'
# Descriptions live with their targets, including pattern targets such as dev-%.
awk -F ':.*## ' '/^[a-zA-Z0-9_.%+-]+:.*## / {printf "  %-28s %s\n", $1, $2}' "$@" | sort
