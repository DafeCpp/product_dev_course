#!/usr/bin/env bash
# Поднять виртуальную CAN-шину для измерительного рига (нужен root).
# Выполняется вручную один раз на сессию:
#   sudo ./setup_vcan.sh
set -euo pipefail

modprobe vcan
ip link add dev vcan0 type vcan 2>/dev/null || true
ip link set up vcan0
ip link show vcan0
echo "vcan0 готов"
