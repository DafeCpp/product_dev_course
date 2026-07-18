# Bench Controller firmware — host-тесты, SIL, измерительный риг и ESP32.
# Запуск из корня: make -C projects/bench_controller/firmware ...

FIRMWARE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
TESTS_DIR    := $(FIRMWARE_DIR)tests
TESTS_BUILD  := $(TESTS_DIR)/build
ESP32_DIR    := $(FIRMWARE_DIR)esp32

# ESP-IDF: задайте IDF_PATH или выполните . $IDF_PATH/export.sh перед make.
ESP32_PORT ?=

.PHONY: all help test test-build test-clean sim-build rig-run \
	esp32-build esp32-flash esp32-monitor esp32-clean

all: help

help:
	@echo "Bench Controller firmware — сборка и тесты"
	@echo ""
	@echo "Host (CI):"
	@echo "  make test        — собрать и запустить host-тесты (GTest)"
	@echo "  make test-build  — только собрать тесты"
	@echo "  make sim-build   — собрать sim_host (SIL CLI)"
	@echo "  make test-clean  — удалить build тестов"
	@echo "  make rig-run     — сценарии на vcan0 (нужны setup_vcan.sh и venv в ../sim)"
	@echo ""
	@echo "ESP32-S3 (нужен ESP-IDF v6.x: . \$$IDF_PATH/export.sh):"
	@echo "  make esp32-build   — собрать прошивку измерительной сессии"
	@echo "  make esp32-flash   [ESP32_PORT=/dev/ttyUSB0] — прошить"
	@echo "  make esp32-monitor — серийный монитор (числа сессии)"
	@echo "  make esp32-clean   — очистить build"

test-build:
	cmake -S $(TESTS_DIR) -B $(TESTS_BUILD) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(TESTS_BUILD) -j

test: test-build
	ctest --test-dir $(TESTS_BUILD) --output-on-failure

sim-build:
	cmake -S $(TESTS_DIR) -B $(TESTS_BUILD) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(TESTS_BUILD) -j --target sim_host
	@echo "sim_host: $(TESTS_BUILD)/sim_host"

test-clean:
	rm -rf $(TESTS_BUILD)

# Измерительный риг (PR-B): vcan0 должен быть поднят (sim/setup_vcan.sh),
# зависимости benchsim установлены (pip install -e ../sim)
rig-run: sim-build
	@ip link show vcan0 >/dev/null 2>&1 || { \
		echo "vcan0 не поднят: sudo $(FIRMWARE_DIR)../sim/setup_vcan.sh"; exit 1; }
	cd $(FIRMWARE_DIR)../sim && \
		{ [ -x .venv/bin/python ] && PY=.venv/bin/python || PY=python3; } && \
		$$PY -m benchsim.scenarios --sim-host $(TESTS_BUILD)/sim_host

# ESP32-S3 — обёртки idf.py (нужен активный ESP-IDF: . $IDF_PATH/export.sh)
esp32-build:
	cd $(ESP32_DIR) && idf.py set-target esp32s3 && idf.py build

esp32-flash:
	cd $(ESP32_DIR) && idf.py $(if $(ESP32_PORT),-p $(ESP32_PORT),) flash

esp32-monitor:
	cd $(ESP32_DIR) && idf.py $(if $(ESP32_PORT),-p $(ESP32_PORT),) monitor

esp32-clean:
	cd $(ESP32_DIR) && idf.py fullclean
