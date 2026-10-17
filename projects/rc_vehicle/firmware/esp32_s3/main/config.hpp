#pragma once

#include "driver/gpio.h"        // GPIO_NUM_*
#include "driver/spi_common.h"  // SPI2_HOST

// Wi-Fi конфигурация
#define WIFI_AP_SSID_PREFIX "RC-Vehicle"
#define WIFI_AP_PASSWORD ""  // Пустой пароль для MVP (можно добавить позже)
#define WIFI_AP_CHANNEL 1
#define WIFI_AP_MAX_CONNECTIONS 4

// HTTP сервер (HTTP + WebSocket на одном порту)
#define HTTP_SERVER_PORT 80

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
#define RC_IN_PULSE_MIN_US 1000         // 1.0 мс
#define RC_IN_PULSE_MAX_US 2000         // 2.0 мс
#define RC_IN_PULSE_NEUTRAL_US 1500     // 1.5 мс
#define RC_IN_PULSE_TOLERANCE_US 200    // Допуск окна приёма вокруг [min, max]
// Таймаут потери сигнала. Вместе с опросом RC (RC_IN_POLL_INTERVAL_MS) и
// FAILSAFE_TIMEOUT_MS даёт время от потери до нейтрали: не больше
// 100 + 20 + 130 = 250 мс (LOS-368).
// = rc_vehicle::config::RcInputConfig::kTimeoutMs.
#define RC_IN_TIMEOUT_MS 100

// IMU конфигурация (MPU-6050/MPU-6500 по SPI)
#define IMU_SPI_HOST SPI2_HOST
#define IMU_SPI_CS_PIN GPIO_NUM_10
#define IMU_SPI_SCK_PIN GPIO_NUM_12
#define IMU_SPI_MOSI_PIN GPIO_NUM_11
#define IMU_SPI_MISO_PIN GPIO_NUM_13
#define IMU_SPI_BAUD_HZ \
  500000  // 500 kHz (1 MHz нестабильно на длинных проводах)

// Магнитометр MMC5983MA — выбор интерфейса:
//   Закомментировать MAG_USE_I2C → SPI (4-проводной, CS на GPIO5)
//   Оставить          MAG_USE_I2C → I2C (2-проводной, SDA/SCL)
// Расположить как можно дальше от мотора/ESC (источников магнитных помех).
#define MAG_USE_I2C

// I2C-конфигурация (активна при MAG_USE_I2C)
#include "driver/i2c_master.h"
#define MAG_I2C_PORT I2C_NUM_0
#define MAG_I2C_SDA_PIN \
  GPIO_NUM_8  // Свободный GPIO (не strapping после загрузки)
#define MAG_I2C_SCL_PIN GPIO_NUM_9  // Свободный GPIO
#define MAG_I2C_BAUD_HZ 400000      // Fast Mode (max 400 кГц)

// SPI-конфигурация (активна без MAG_USE_I2C)
#define MAG_SPI_HOST SPI2_HOST
#define MAG_SPI_CS_PIN GPIO_NUM_5
#define MAG_SPI_SCK_PIN IMU_SPI_SCK_PIN  // Общие линии шины
#define MAG_SPI_MOSI_PIN IMU_SPI_MOSI_PIN
#define MAG_SPI_MISO_PIN IMU_SPI_MISO_PIN
#define MAG_SPI_BAUD_HZ 1000000  // 1 МГц (max 10 МГц)

// Тайминги (в миллисекундах)
#define CONTROL_LOOP_PERIOD_MS 2   // 500 Hz — основной цикл Core 1
#define PWM_UPDATE_INTERVAL_MS 20  // 50 Hz (каждые 10 итераций control loop)
#define RC_IN_POLL_INTERVAL_MS 20  // 50 Hz
#define IMU_READ_INTERVAL_MS 2     // 500 Hz (каждую итерацию control loop)
#define TELEM_SEND_INTERVAL_MS 50  // 20 Hz
// Grace-период failsafe после потери последнего источника управления; сам
// источник теряется по RC_IN_TIMEOUT_MS (+ опрос RC) или
// WifiConfig::kCommandTimeoutMs, в сумме не больше 250 мс (LOS-368).
// = rc_vehicle::config::FailsafeConfig::kTimeoutMs.
#define FAILSAFE_TIMEOUT_MS 130

// Потеря RC → нейтраль не позже 250 мс (TESTING_PROGRAM.md, LOS-368).
static_assert(RC_IN_TIMEOUT_MS + RC_IN_POLL_INTERVAL_MS + FAILSAFE_TIMEOUT_MS <=
                  250,
              "RC loss to neutral must stay within 250 ms");

// Slew-rate limiting (плавность)
#define SLEW_RATE_THROTTLE_MAX_PER_SEC 0.5f
#define SLEW_RATE_STEERING_MAX_PER_SEC 1.0f
