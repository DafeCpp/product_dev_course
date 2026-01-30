# Сборка, заливка и мониторинг всех прошивок RC Vehicle
# Запуск из корня репозитория: make -C projects/rc_vehicle/firmware ...
# Или из firmware/: make ...

FIRMWARE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
ESP32_DIR    := $(FIRMWARE_DIR)esp32
RP2040_DIR   := $(FIRMWARE_DIR)rp2040
STM32_DIR    := $(FIRMWARE_DIR)stm32

# Порты (опционально): задайте при заливке/мониторе, если автоопределение не подходит
# ESP32:    make flash-esp32 ESP32_PORT=/dev/cu.usbserial-0001
# RP2040:   make flash-rp2040 RP2040_UF2_VOL=/Volumes/RPI-RP2  ;  make monitor-rp2040 RP2040_PORT=/dev/cu.usbmodem*
# STM32:    make flash-stm32  (st-flash)  ;  make monitor-stm32 STM32_PORT=/dev/cu.usbserial-*
ESP32_PORT      ?=
RP2040_PORT     ?=
RP2040_UF2_VOL  ?= /Volumes/RPI-RP2
STM32_PORT      ?=

# STM32: MCU по умолчанию (STM32F103C8, STM32F401CE, STM32F411CE, STM32G431CB)
STM32_MCU ?= STM32F103C8

# STM32Cube LL: пути к пакетам по семейству (F1/F4/G4). Задайте тот, что нужен для MCU.
# Пример: make build-stm32 STM32CUBE_F1_PATH=$$HOME/STM32CubeF1
STM32CUBE_F1_PATH ?=
STM32CUBE_F4_PATH ?=
STM32CUBE_G4_PATH ?=

# ESP-IDF: если задан IDF_PATH, make подгрузит окружение (export.sh) перед вызовом idf.py
# Пример: make build-esp32 IDF_PATH=$$HOME/esp/esp-idf
IDF_PATH ?=

.PHONY: all build-all build-esp32 build-rp2040 build-stm32
.PHONY: clean clean-esp32 clean-rp2040 clean-stm32 clean-all
.PHONY: flash-esp32 flash-rp2040 flash-stm32
.PHONY: monitor-esp32 monitor-rp2040 monitor-stm32
.PHONY: flash-monitor-esp32 flash-monitor-rp2040 flash-monitor-stm32
.PHONY: help

# По умолчанию — справка
all: help

help:
	@echo "RC Vehicle firmware — сборка, заливка, монитор"
	@echo ""
	@echo "Сборка:"
	@echo "  make build-all       — собрать все прошивки (ESP32, RP2040, STM32)"
	@echo "  make build-esp32     — только ESP32 (задайте IDF_PATH или . export.sh в shell)"
	@echo "  make build-rp2040   — только RP2040 (нужен PICO_SDK_PATH)"
	@echo "  make build-stm32    — только STM32 (задайте STM32CUBE_F1_PATH или F4/G4 для MCU, опц. STM32_MCU=...)"
	@echo ""
	@echo "Очистка:"
	@echo "  make clean-all       — очистить все платформы"
	@echo "  make clean-esp32    make clean-rp2040    make clean-stm32"
	@echo ""
	@echo "Заливка (flash):"
	@echo "  make flash-esp32    [ESP32_PORT=/dev/cu.usbserial-XXX]"
	@echo "  make flash-rp2040   (копирование .uf2 в RPI-RP2; задайте RP2040_UF2_VOL при другом пути)"
	@echo "  make flash-stm32    (st-flash; перед этим make build-stm32)"
	@echo ""
	@echo "Монитор логов (после заливки):"
	@echo "  make monitor-esp32   [ESP32_PORT=...]   — выход: Ctrl+]"
	@echo "  make monitor-rp2040 [RP2040_PORT=...]   — 115200 8N1"
	@echo "  make monitor-stm32  [STM32_PORT=...]   — 115200 8N1 (если UART выведен)"
	@echo ""
	@echo "Заливка + монитор одной командой:"
	@echo "  make flash-monitor-esp32   make flash-monitor-rp2040   make flash-monitor-stm32"
	@echo ""
	@echo "Переменные: IDF_PATH, STM32CUBE_F1_PATH/F4/G4, ESP32_PORT, RP2040_PORT, RP2040_UF2_VOL, STM32_PORT, STM32_MCU"

# --- Сборка ---
build-all: build-esp32 build-rp2040 build-stm32

# Подгрузить ESP-IDF (если IDF_PATH задан) и выполнить idf.py; при отсутствии idf.py — подсказка
define run_idf
	export IDF_PATH="$(IDF_PATH)"; \
	cd "$(ESP32_DIR)" && bash -c '\
	if [ -n "$$IDF_PATH" ] && [ -f "$$IDF_PATH/export.sh" ]; then . "$$IDF_PATH/export.sh"; fi; \
	if ! command -v idf.py >/dev/null 2>&1; then \
		echo ">>> idf.py не найден. Задайте путь к ESP-IDF:"; \
		echo "    make build-esp32 IDF_PATH=/path/to/esp-idf"; \
		echo "   Или в текущем терминале: . \$$IDF_PATH/export.sh"; \
		exit 127; \
	fi; \
	exec idf.py $(1)'
endef

build-esp32:
	@echo ">>> Сборка ESP32..."
	@$(call run_idf,build)

build-rp2040:
	@echo ">>> Сборка RP2040..."
	@cd "$(RP2040_DIR)" && $(MAKE) all

build-stm32:
	@echo ">>> Сборка STM32 (MCU=$(STM32_MCU))..."
	@$(if $(filter STM32F103C8,$(STM32_MCU)),$(if $(STM32CUBE_F1_PATH),,$(error Для STM32F103C8 задайте STM32CUBE_F1_PATH=/path/to/STM32CubeF1)))
	@$(if $(filter STM32F401CE STM32F411CE,$(STM32_MCU)),$(if $(STM32CUBE_F4_PATH),,$(error Для $(STM32_MCU) задайте STM32CUBE_F4_PATH=/path/to/STM32CubeF4)))
	@$(if $(filter STM32G431CB,$(STM32_MCU)),$(if $(STM32CUBE_G4_PATH),,$(error Для STM32G431CB задайте STM32CUBE_G4_PATH=/path/to/STM32CubeG4)))
	@cd "$(STM32_DIR)" && $(MAKE) all MCU=$(STM32_MCU) \
		STM32CUBE_F1_PATH="$(STM32CUBE_F1_PATH)" \
		STM32CUBE_F4_PATH="$(STM32CUBE_F4_PATH)" \
		STM32CUBE_G4_PATH="$(STM32CUBE_G4_PATH)"

# --- Очистка ---
clean-all: clean-esp32 clean-rp2040 clean-stm32

clean-esp32:
	@echo ">>> Очистка ESP32..."
	@export IDF_PATH="$(IDF_PATH)"; cd "$(ESP32_DIR)" && bash -c 'if [ -n "$$IDF_PATH" ] && [ -f "$$IDF_PATH/export.sh" ]; then . "$$IDF_PATH/export.sh"; fi; idf.py fullclean 2>/dev/null' || true

clean-rp2040:
	@echo ">>> Очистка RP2040..."
	@cd "$(RP2040_DIR)" && $(MAKE) clean

clean-stm32:
	@echo ">>> Очистка STM32..."
	@cd "$(STM32_DIR)" && $(MAKE) clean

# --- Заливка ---
flash-esp32: build-esp32
	@echo ">>> Заливка ESP32..."
	@$(call run_idf,$(if $(ESP32_PORT),-p $(ESP32_PORT),) flash)

flash-rp2040: build-rp2040
	@echo ">>> Заливка RP2040 (копирование .uf2 в $(RP2040_UF2_VOL))..."
	@if [ -d "$(RP2040_UF2_VOL)" ]; then \
		cp "$(RP2040_DIR)/build/rc_vehicle_rp2040.uf2" "$(RP2040_UF2_VOL)/" && \
		echo "Скопировано. Отключите и подключите Pico для перезагрузки."; \
	else \
		echo "Том RPI-RP2 не найден. Зажмите BOOTSEL на Pico, подключите USB и повторите."; \
		echo "Или укажите том: make flash-rp2040 RP2040_UF2_VOL=/путь/к/RPI-RP2"; \
		exit 1; \
	fi

flash-stm32: build-stm32
	@echo ">>> Заливка STM32 (st-flash)..."
	@command -v st-flash >/dev/null 2>&1 || { echo "Установите stlink: brew install stlink (или apt install stlink-tools)"; exit 1; }
	@st-flash write "$(STM32_DIR)/build/rc_vehicle_stm32.bin" 0x08000000

# --- Монитор ---
monitor-esp32:
	@echo ">>> Монитор ESP32 (выход: Ctrl+])..."
	@$(call run_idf,$(if $(ESP32_PORT),-p $(ESP32_PORT),) monitor)

monitor-rp2040:
	@echo ">>> Монитор RP2040 (115200). Выход: Ctrl+A K (screen) или Ctrl+C (miniterm)..."
	@if [ -z "$(RP2040_PORT)" ]; then echo "Задайте порт: make monitor-rp2040 RP2040_PORT=/dev/cu.usbmodem101"; exit 1; fi
	@if command -v screen >/dev/null 2>&1; then \
		screen "$(RP2040_PORT)" 115200; \
	elif python3 -c "import serial.tools.miniterm" 2>/dev/null; then \
		python3 -m serial.tools.miniterm "$(RP2040_PORT)" 115200; \
	else \
		echo "Установите screen (brew install screen) или pyserial (pip install pyserial)"; exit 1; \
	fi

monitor-stm32:
	@echo ">>> Монитор STM32 (115200). Выход: Ctrl+A K (screen) или Ctrl+C (miniterm)..."
	@if [ -z "$(STM32_PORT)" ]; then echo "Задайте порт: make monitor-stm32 STM32_PORT=/dev/cu.usbserial-XXX"; exit 1; fi
	@if command -v screen >/dev/null 2>&1; then \
		screen "$(STM32_PORT)" 115200; \
	elif python3 -c "import serial.tools.miniterm" 2>/dev/null; then \
		python3 -m serial.tools.miniterm "$(STM32_PORT)" 115200; \
	else \
		echo "Установите screen или pyserial"; exit 1; \
	fi

# --- Заливка + монитор ---
flash-monitor-esp32: flash-esp32
	@$(call run_idf,$(if $(ESP32_PORT),-p $(ESP32_PORT),) monitor)

flash-monitor-rp2040: flash-rp2040
	@sleep 2
	@$(MAKE) monitor-rp2040

flash-monitor-stm32: flash-stm32
	@$(MAKE) monitor-stm32
