#!/usr/bin/env sh

# Keep an existing generated sdkconfig aligned with sdkconfig.defaults.
# ESP-IDF does not overwrite an already selected Kconfig choice from defaults.

set -eu

sdkconfig_path=${1:-sdkconfig}

if [ ! -f "$sdkconfig_path" ]; then
  exit 0
fi

if ! grep -Eq '^CONFIG_(ESP_DEFAULT_CPU_FREQ_MHZ|ESP32S3_DEFAULT_CPU_FREQ_MHZ)=(80|160)$' \
    "$sdkconfig_path"; then
  exit 0
fi

temporary_path="${sdkconfig_path}.los-242.tmp"
trap 'rm -f "$temporary_path"' EXIT HUP INT TERM

awk '
  /^CONFIG_(ESP_DEFAULT_CPU_FREQ_MHZ|ESP32S3_DEFAULT_CPU_FREQ)_(80|160)=y$/ {
    sub(/^CONFIG_/, "# CONFIG_")
    sub(/=y$/, " is not set")
  }
  /^# CONFIG_(ESP_DEFAULT_CPU_FREQ_MHZ|ESP32S3_DEFAULT_CPU_FREQ)_240 is not set$/ {
    sub(/^# /, "")
    sub(/ is not set$/, "=y")
  }
  /^CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=(80|160)$/ {
    print "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240"
    next
  }
  /^CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ=(80|160)$/ {
    print "CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ=240"
    next
  }
  { print }
' "$sdkconfig_path" > "$temporary_path"

mv "$temporary_path" "$sdkconfig_path"
trap - EXIT HUP INT TERM

echo ">>> Обновлён ${sdkconfig_path}: CPU ESP32-S3 = 240 МГц"
