# Makefile для сборки прошивки RC Vehicle (ESP32-S3)
# Делегирует в firmware/Makefile

.PHONY: help all build clean flash monitor flash-monitor

FIRMWARE_DIR := firmware

help:
	@$(MAKE) -C $(FIRMWARE_DIR) help

all: help

build:
	@$(MAKE) -C $(FIRMWARE_DIR) build

clean:
	@$(MAKE) -C $(FIRMWARE_DIR) clean

flash:
	@$(MAKE) -C $(FIRMWARE_DIR) flash

monitor:
	@$(MAKE) -C $(FIRMWARE_DIR) monitor

flash-monitor:
	@$(MAKE) -C $(FIRMWARE_DIR) flash-monitor
