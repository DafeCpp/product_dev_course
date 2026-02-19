# Сборка, заливка и мониторинг прошивки RC Vehicle (ESP32-S3)
# Запуск из корня репозитория: make -C projects/rc_vehicle/firmware ...
# Или из firmware/: make ...

FIRMWARE_DIR   := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
ESP32_S3_DIR   := $(FIRMWARE_DIR)esp32_s3

# Порт (опционально): задайте при заливке/мониторе, если автоопределение не подходит
# make flash ESP32_S3_PORT=/dev/cu.usbserial-0002
ESP32_S3_PORT ?=

# ESP-IDF: если задан IDF_PATH, make подгрузит окружение (export.sh) перед вызовом idf.py
IDF_PATH ?=

.PHONY: all build clean flash monitor flash-monitor help

# По умолчанию — справка
all: help

help:
	@echo "RC Vehicle firmware (ESP32-S3) — сборка, заливка, монитор"
	@echo ""
	@echo "Сборка:"
	@echo "  make build   — собрать прошивку (задайте IDF_PATH или . export.sh в shell)"
	@echo ""
	@echo "Очистка:"
	@echo "  make clean   — очистить сборку"
	@echo ""
	@echo "Заливка:"
	@echo "  make flash   [ESP32_S3_PORT=/dev/cu.usbserial-XXX]"
	@echo ""
	@echo "Монитор логов (выход: Ctrl+]):"
	@echo "  make monitor [ESP32_S3_PORT=...]"
	@echo ""
	@echo "Заливка + монитор:"
	@echo "  make flash-monitor"
	@echo ""
	@echo "Переменные: IDF_PATH, ESP32_S3_PORT"

# Подгрузить ESP-IDF (если IDF_PATH задан) и выполнить idf.py
define run_idf
	export IDF_PATH="$(IDF_PATH)"; \
	cd "$(ESP32_S3_DIR)" && bash -c '\
	if [ -n "$$IDF_PATH" ] && [ -f "$$IDF_PATH/export.sh" ]; then . "$$IDF_PATH/export.sh"; fi; \
	if ! command -v idf.py >/dev/null 2>&1; then \
		echo ">>> idf.py не найден. Задайте путь к ESP-IDF:"; \
		echo "    make build IDF_PATH=/path/to/esp-idf"; \
		echo "   Или в текущем терминале: . \$$IDF_PATH/export.sh"; \
		exit 127; \
	fi; \
	exec idf.py $(1)'
endef

build:
	@echo ">>> Сборка ESP32-S3..."
	@$(call run_idf,build)

clean:
	@echo ">>> Очистка ESP32-S3..."
	@export IDF_PATH="$(IDF_PATH)"; cd "$(ESP32_S3_DIR)" && bash -c 'if [ -n "$$IDF_PATH" ] && [ -f "$$IDF_PATH/export.sh" ]; then . "$$IDF_PATH/export.sh"; fi; idf.py fullclean 2>/dev/null' || true

flash: build
	@echo ">>> Заливка ESP32-S3..."
	@$(call run_idf,$(if $(ESP32_S3_PORT),-p $(ESP32_S3_PORT),) flash)

monitor:
	@echo ">>> Монитор ESP32-S3 (выход: Ctrl+])..."
	@$(call run_idf,$(if $(ESP32_S3_PORT),-p $(ESP32_S3_PORT),) monitor)

flash-monitor: flash
	@$(call run_idf,$(if $(ESP32_S3_PORT),-p $(ESP32_S3_PORT),) monitor)
