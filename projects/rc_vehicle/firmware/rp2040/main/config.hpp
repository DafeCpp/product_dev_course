#pragma once

// UART конфигурация (RP2040 ↔ ESP32)
#define UART_ID uart0
#define UART_BAUD_RATE 115200
#define UART_TX_PIN 0  // GPIO0 для TX (настраивается по схеме)
#define UART_RX_PIN 1  // GPIO1 для RX (настраивается по схеме)
#define UART_BUF_SIZE 1024

// PWM конфигурация
#define PWM_FREQUENCY_HZ 50  // 50 Hz для RC серво/ESC
#define PWM_THROTTLE_PIN 2   // GPIO2 для ESC (настраивается по схеме)
#define PWM_STEERING_PIN 3   // GPIO3 для серво (настраивается по схеме)

// RC-in конфигурация (чтение сигналов с RC приёмника)
#define RC_IN_THROTTLE_PIN 4  // GPIO4 для канал газа (настраивается по схеме)
#define RC_IN_STEERING_PIN 5  // GPIO5 для канал руля (настраивается по схеме)
#define RC_IN_PULSE_MIN_US 1000      // Минимальная ширина импульса (1 мс)
#define RC_IN_PULSE_MAX_US 2000      // Максимальная ширина импульса (2 мс)
#define RC_IN_PULSE_NEUTRAL_US 1500  // Нейтральное значение (1.5 мс)
#define RC_IN_TIMEOUT_MS 250         // Таймаут потери сигнала (250 мс)

// IMU конфигурация (SPI, MPU-6050). Пины не пересекаются с UART(0,1), PWM(2,3),
// RC-in(4,5)
#define SPI_ID spi0
#define SPI_CS_PIN 8         // Chip Select (настраивается по схеме)
#define SPI_SCK_PIN 6        // SPI SCK
#define SPI_MOSI_PIN 7       // SPI MOSI
#define SPI_MISO_PIN 9       // SPI MISO
#define SPI_BAUD_HZ 1000000  // 1 MHz для MPU-6050

// Тайминги (в миллисекундах)
#define PWM_UPDATE_INTERVAL_MS 20  // 50 Hz - частота обновления PWM
#define RC_IN_POLL_INTERVAL_MS 20  // 50 Hz - частота опроса RC-in
#define IMU_READ_INTERVAL_MS 20    // 50 Hz - частота чтения IMU
#define TELEM_SEND_INTERVAL_MS 50  // 20 Hz - частота отправки телеметрии
#define FAILSAFE_TIMEOUT_MS 250    // Таймаут failsafe (250 мс)

// Константы протокола UART — в firmware/common/protocol.hpp

// PWM значения (микросекунды)
#define PWM_NEUTRAL_US 1500  // Нейтраль (1.5 мс)
#define PWM_MIN_US 1000      // Минимум (1 мс)
#define PWM_MAX_US 2000      // Максимум (2 мс)

// Slew-rate limiting (опционально)
#define SLEW_RATE_THROTTLE_MAX_PER_SEC \
  0.5f  // Максимальная скорость изменения газа
#define SLEW_RATE_STEERING_MAX_PER_SEC \
  1.0f  // Максимальная скорость изменения руля

// Отладка
#define DEBUG_ENABLED 1

// Общий конфиг (переиспользуемые таймауты/дефолты).
#include "../../common/config_common.hpp"
