#pragma once

#include "driver/gpio.h"        // GPIO_NUM_*
#include "driver/spi_common.h"  // SPI2_HOST

// Wi-Fi конфигурация
#define WIFI_AP_SSID_PREFIX "RC-Vehicle"
#define WIFI_AP_PASSWORD ""  // Пустой пароль для MVP (можно добавить позже)
#define WIFI_AP_CHANNEL 1
#define WIFI_AP_MAX_CONNECTIONS 4

// HTTP сервер
#define HTTP_SERVER_PORT 80

// WebSocket сервер
#define WEBSOCKET_SERVER_PORT 81
#define WEBSOCKET_MAX_CLIENTS 4

// Размеры буферов WebSocket (RX — входящие команды от браузера)
#define WS_RX_BUFFER_SIZE 1024

// PWM конфигурация (ESC + servo), 50 Hz, 1–2 мс
#define PWM_FREQUENCY_HZ 50
#define PWM_THROTTLE_PIN GPIO_NUM_14  // ESC (Signal)
#define PWM_STEERING_PIN GPIO_NUM_21  // Servo (Signal)

// PWM значения (микросекунды)
#define PWM_NEUTRAL_US 1500  // Нейтраль (1.5 мс)
#define PWM_MIN_US 1000      // Минимум (1.0 мс)
#define PWM_MAX_US 2000      // Максимум (2.0 мс)

// RC-in конфигурация (чтение PWM с приёмника)
#define RC_IN_THROTTLE_PIN GPIO_NUM_16  // CH1
#define RC_IN_STEERING_PIN GPIO_NUM_17  // CH2
#define RC_IN_PULSE_MIN_US 1000        // 1.0 мс
#define RC_IN_PULSE_MAX_US 2000        // 2.0 мс
#define RC_IN_PULSE_NEUTRAL_US 1500    // 1.5 мс
#define RC_IN_TIMEOUT_MS 250           // Таймаут потери сигнала

// IMU конфигурация (MPU-6050/MPU-6500 по SPI)
#define IMU_SPI_HOST SPI2_HOST
#define IMU_SPI_CS_PIN GPIO_NUM_10
#define IMU_SPI_SCK_PIN GPIO_NUM_12
#define IMU_SPI_MOSI_PIN GPIO_NUM_11
#define IMU_SPI_MISO_PIN GPIO_NUM_13
#define IMU_SPI_BAUD_HZ 500000  // 500 kHz (1 MHz нестабильно на длинных проводах)

// Тайминги (в миллисекундах)
#define CONTROL_LOOP_PERIOD_MS 2   // 500 Hz — основной цикл Core 1
#define PWM_UPDATE_INTERVAL_MS 20  // 50 Hz (каждые 10 итераций control loop)
#define RC_IN_POLL_INTERVAL_MS 20  // 50 Hz
#define IMU_READ_INTERVAL_MS 2     // 500 Hz (каждую итерацию control loop)
#define TELEM_SEND_INTERVAL_MS 50  // 20 Hz
#define FAILSAFE_TIMEOUT_MS 250    // Таймаут failsafe

// Slew-rate limiting (плавность)
#define SLEW_RATE_THROTTLE_MAX_PER_SEC 0.5f
#define SLEW_RATE_STEERING_MAX_PER_SEC 1.0f

// Общий конфиг (переиспользуемые таймауты/дефолты).
#include "../../common/config_common.hpp"
