# Bench Controller firmware — host-тесты и SIL (PR-A).
# ESP32-цели добавятся вместе с esp32/ (фаза C спайка LOS-76).
# Запуск из корня: make -C projects/bench_controller/firmware ...

FIRMWARE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
TESTS_DIR    := $(FIRMWARE_DIR)tests
TESTS_BUILD  := $(TESTS_DIR)/build

.PHONY: all help test test-build test-clean sim-build

all: help

help:
	@echo "Bench Controller firmware — сборка и тесты"
	@echo ""
	@echo "  make test        — собрать и запустить host-тесты (GTest)"
	@echo "  make test-build  — только собрать тесты"
	@echo "  make sim-build   — собрать sim_host (SIL CLI)"
	@echo "  make test-clean  — удалить build тестов"

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
