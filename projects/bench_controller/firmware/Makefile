# Bench Controller firmware — host-тесты, SIL и измерительный риг.
# ESP32-цели добавятся вместе с esp32/ (фаза C спайка LOS-76).
# Запуск из корня: make -C projects/bench_controller/firmware ...

FIRMWARE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
TESTS_DIR    := $(FIRMWARE_DIR)tests
TESTS_BUILD  := $(TESTS_DIR)/build

.PHONY: all help test test-build test-clean sim-build rig-run

all: help

help:
	@echo "Bench Controller firmware — сборка и тесты"
	@echo ""
	@echo "  make test        — собрать и запустить host-тесты (GTest)"
	@echo "  make test-build  — только собрать тесты"
	@echo "  make sim-build   — собрать sim_host (SIL CLI)"
	@echo "  make test-clean  — удалить build тестов"
	@echo "  make rig-run     — сценарии на vcan0 (нужны setup_vcan.sh и venv в ../sim)"

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
