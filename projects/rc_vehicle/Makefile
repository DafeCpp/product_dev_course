# Makefile для сборки прошивок RC Vehicle
# Поддерживает сборку ESP32 и RP2040 прошивок

.PHONY: help all all-force clean check-esp-idf check-rp2040-toolchain esp32-build esp32-flash esp32-monitor esp32-clean esp32-set-target rp2040-build rp2040-clean rp2040-flash

# Цвета для вывода
GREEN := \033[0;32m
YELLOW := \033[0;33m
RED := \033[0;31m
NC := \033[0m # No Color

# Директории
ESP32_DIR := firmware/esp32
RP2040_DIR := firmware/rp2040
RP2040_BUILD_DIR := $(RP2040_DIR)/build

help:
	@echo "$(GREEN)Доступные команды для сборки прошивок RC Vehicle:$(NC)"
	@echo ""
	@echo "$(YELLOW)ESP32 прошивка:$(NC)"
	@echo "  make esp32-set-target    - Установить целевую плату (esp32s3) - обязательно перед первой сборкой!"
	@echo "  make esp32-build         - Собрать прошивку ESP32"
	@echo "  make esp32-flash         - Прошить ESP32 (требует подключенного устройства)"
	@echo "  make esp32-monitor       - Запустить мониторинг ESP32"
	@echo "  make esp32-clean         - Очистить проект ESP32"
	@echo ""
	@echo "$(YELLOW)RP2040 прошивка:$(NC)"
	@echo "  make rp2040-build        - Собрать прошивку RP2040"
	@echo "  make rp2040-clean        - Очистить проект RP2040"
	@echo "  make rp2040-flash         - Прошить RP2040 (скопировать .uf2 файл)"
	@echo ""
	@echo "$(YELLOW)Общие команды:$(NC)"
	@echo "  make all                 - Собрать обе прошивки (продолжает при ошибках)"
	@echo "  make all-force           - Собрать обе прошивки (останавливается при ошибке)"
	@echo "  make clean               - Очистить обе прошивки"
	@echo ""
	@echo "$(YELLOW)Требования:$(NC)"
	@echo "  ESP32:   ESP-IDF v5.5 должен быть установлен и активирован (get_idf)"
	@echo "  RP2040:  Компилятор arm-none-eabi-gcc (установка: sudo apt-get install gcc-arm-none-eabi)"
	@echo "           Pico SDK будет автоматически загружен из Git, если PICO_SDK_PATH не установлен"
	@echo "           Или установите PICO_SDK_PATH для использования локального SDK"
	@echo ""

# ============================================================================
# ESP32 прошивка
# ============================================================================

# Проверка наличия ESP-IDF
check-esp-idf:
	@if ! command -v idf.py >/dev/null 2>&1 && [ -z "$$IDF_PATH" ]; then \
		echo "$(RED)Ошибка: ESP-IDF не активирован.$(NC)"; \
		echo "$(YELLOW)Выполните одну из команд:$(NC)"; \
		echo "  - get_idf  (если алиас настроен в ~/.bashrc)"; \
		echo "  - . \$$HOME/esp/esp-idf/export.sh  (если ESP-IDF в стандартном месте)"; \
		echo "  - . /path/to/esp-idf/export.sh  (укажите путь к ESP-IDF)"; \
		echo ""; \
		echo "$(YELLOW)Или соберите только RP2040: make rp2040-build$(NC)"; \
		exit 1; \
	fi

esp32-set-target: check-esp-idf
	@echo "$(GREEN)Установка целевой платы ESP32-S3...$(NC)"
	@cd $(ESP32_DIR) && idf.py set-target esp32s3

esp32-build: check-esp-idf
	@echo "$(GREEN)Сборка прошивки ESP32...$(NC)"
	@cd $(ESP32_DIR) && idf.py build
	@echo "$(GREEN)Сборка ESP32 завершена!$(NC)"

esp32-flash: check-esp-idf
	@echo "$(GREEN)Прошивка ESP32...$(NC)"
	@cd $(ESP32_DIR) && idf.py flash

esp32-monitor: check-esp-idf
	@echo "$(GREEN)Запуск мониторинга ESP32...$(NC)"
	@cd $(ESP32_DIR) && idf.py monitor

esp32-clean:
	@echo "$(YELLOW)Очистка проекта ESP32...$(NC)"
	@if command -v idf.py >/dev/null 2>&1 || [ -n "$$IDF_PATH" ]; then \
		cd $(ESP32_DIR) && idf.py fullclean; \
	else \
		echo "$(YELLOW)ESP-IDF не активирован, удаляю build директорию вручную...$(NC)"; \
		rm -rf $(ESP32_DIR)/build; \
	fi
	@echo "$(GREEN)Очистка ESP32 завершена!$(NC)"

# ============================================================================
# RP2040 прошивка
# ============================================================================

# Проверка наличия компилятора для RP2040
check-rp2040-toolchain:
	@if ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then \
		echo "$(RED)Ошибка: компилятор arm-none-eabi-gcc не найден.$(NC)"; \
		echo "$(YELLOW)Установите инструменты для сборки RP2040:$(NC)"; \
		echo ""; \
		echo "$(YELLOW)Для Ubuntu/Debian:$(NC)"; \
		echo "  sudo apt-get update"; \
		echo "  sudo apt-get install gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential"; \
		echo ""; \
		echo "$(YELLOW)Для других дистрибутивов:$(NC)"; \
		echo "  Установите пакет gcc-arm-none-eabi из репозитория вашего дистрибутива"; \
		echo "  Или скачайте с https://developer.arm.com/downloads/-/gnu-rm"; \
		echo ""; \
		exit 1; \
	fi

rp2040-build: check-rp2040-toolchain
	@echo "$(GREEN)Сборка прошивки RP2040...$(NC)"
	@if [ -z "$$PICO_SDK_PATH" ]; then \
		echo "$(YELLOW)PICO_SDK_PATH не установлен. Включаю автоматическую загрузку SDK из Git...$(NC)"; \
		echo "$(YELLOW)Это может занять некоторое время при первой сборке.$(NC)"; \
	fi
	@mkdir -p $(RP2040_BUILD_DIR)
	@if [ -z "$$PICO_SDK_PATH" ]; then \
		cd $(RP2040_BUILD_DIR) && cmake -DPICO_SDK_FETCH_FROM_GIT=on .. && make; \
	else \
		cd $(RP2040_BUILD_DIR) && cmake .. && make; \
	fi
	@if [ -f $(RP2040_BUILD_DIR)/main/rc_vehicle_rp2040.uf2 ]; then \
		echo "$(GREEN)Сборка RP2040 завершена! Файл: $(RP2040_BUILD_DIR)/main/rc_vehicle_rp2040.uf2$(NC)"; \
	elif [ -f $(RP2040_BUILD_DIR)/rc_vehicle_rp2040.uf2 ]; then \
		echo "$(GREEN)Сборка RP2040 завершена! Файл: $(RP2040_BUILD_DIR)/rc_vehicle_rp2040.uf2$(NC)"; \
	else \
		echo "$(RED)Ошибка: файл .uf2 не найден после сборки$(NC)"; \
		echo "$(YELLOW)Проверьте директорию: $(RP2040_BUILD_DIR)/main/$(NC)"; \
		exit 1; \
	fi

rp2040-clean:
	@echo "$(YELLOW)Очистка проекта RP2040...$(NC)"
	@rm -rf $(RP2040_BUILD_DIR)
	@echo "$(GREEN)Очистка RP2040 завершена!$(NC)"

rp2040-flash:
	@echo "$(GREEN)Инструкция по прошивке RP2040:$(NC)"
	@echo "1. Зажмите кнопку BOOTSEL на Raspberry Pi Pico"
	@echo "2. Подключите Pico к компьютеру через USB"
	@echo "3. Отпустите кнопку BOOTSEL"
	@echo "4. Скопируйте файл .uf2 на диск RPI-RP2"
	@echo ""
	@UF2_FILE=""; \
	if [ -f $(RP2040_BUILD_DIR)/main/rc_vehicle_rp2040.uf2 ]; then \
		UF2_FILE="$(RP2040_BUILD_DIR)/main/rc_vehicle_rp2040.uf2"; \
	elif [ -f $(RP2040_BUILD_DIR)/rc_vehicle_rp2040.uf2 ]; then \
		UF2_FILE="$(RP2040_BUILD_DIR)/rc_vehicle_rp2040.uf2"; \
	fi; \
	if [ -n "$$UF2_FILE" ]; then \
		echo "$(GREEN)Файл прошивки найден: $$UF2_FILE$(NC)"; \
		echo "$(YELLOW)Используйте команду для копирования (замените /dev/sdX на ваш диск):$(NC)"; \
		echo "  sudo cp $$UF2_FILE /media/\$$USER/RPI-RP2/"; \
		echo ""; \
		echo "$(YELLOW)Или используйте автоматическое определение диска:$(NC)"; \
		echo "  sudo cp $$UF2_FILE /media/\$$USER/RPI-RP2/ 2>/dev/null || sudo cp $$UF2_FILE /run/media/\$$USER/RPI-RP2/"; \
	else \
		echo "$(RED)Ошибка: файл прошивки не найден. Выполните: make rp2040-build$(NC)"; \
		exit 1; \
	fi

# ============================================================================
# Общие команды
# ============================================================================

all:
	@echo "$(GREEN)Сборка всех прошивок...$(NC)"
	@echo ""
	@$(MAKE) -k esp32-build rp2040-build || true
	@echo ""
	@echo "$(GREEN)Попытка сборки завершена. Проверьте результаты выше.$(NC)"
	@echo "$(YELLOW)Примечания:$(NC)"
	@echo "$(YELLOW)  - Если ESP-IDF не активирован, ESP32 не соберётся. Выполните: get_idf$(NC)"
	@echo "$(YELLOW)  - Если arm-none-eabi-gcc не установлен, RP2040 не соберётся. Установите: sudo apt-get install gcc-arm-none-eabi$(NC)"
	@echo "$(YELLOW)Используйте 'make esp32-build' или 'make rp2040-build' для сборки отдельных прошивок.$(NC)"

all-force:
	@echo "$(GREEN)Сборка всех прошивок (с остановкой при ошибке)...$(NC)"
	@$(MAKE) esp32-build rp2040-build
	@echo "$(GREEN)Все прошивки собраны!$(NC)"

clean: esp32-clean rp2040-clean
	@echo "$(GREEN)Все проекты очищены!$(NC)"

