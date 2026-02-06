#pragma once

// Общая конфигурация (не зависит от MCU)
#define UART_BAUD_RATE 115200
#define UART_BUF_SIZE 1024

#define PWM_FREQUENCY_HZ 50
#define RC_IN_PULSE_MIN_US 1000
#define RC_IN_PULSE_MAX_US 2000
#define RC_IN_PULSE_NEUTRAL_US 1500
#define RC_IN_TIMEOUT_MS 250

// IMU (MPU-6050) по SPI — пины и периферия в board_pins.hpp
#define IMU_SPI_PRESCALER 32  // APB clock / 32 (настраивается под частоту шины)

#define PWM_UPDATE_INTERVAL_MS 20
#define RC_IN_POLL_INTERVAL_MS 20
#define IMU_READ_INTERVAL_MS 20
#define TELEM_SEND_INTERVAL_MS 50
#define FAILSAFE_TIMEOUT_MS 250

// Константы протокола UART — в firmware/common/protocol.hpp

#define PWM_NEUTRAL_US 1500
#define PWM_MIN_US 1000
#define PWM_MAX_US 2000

#define SLEW_RATE_THROTTLE_MAX_PER_SEC 0.5f
#define SLEW_RATE_STEERING_MAX_PER_SEC 1.0f

#define DEBUG_ENABLED 1

// Пины и периферия задаются в board_pins.hpp (подключается по MCU)
#include "board_pins.hpp"

// Общий конфиг (переиспользуемые таймауты/дефолты).
#include "../../common/config_common.hpp"
