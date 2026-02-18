#pragma once

#include <cstdint>

namespace rc_vehicle::config {

/**
 * @brief Конфигурация control loop
 */
struct ControlLoopConfig {
  static constexpr uint32_t kPeriodMs = 2;  ///< Период control loop (500 Hz)
  static constexpr uint32_t kStackSize = 12288;  ///< Размер стека задачи
  static constexpr uint32_t kPriority = 5;       ///< Приоритет задачи
};

/**
 * @brief Конфигурация RC input
 */
struct RcInputConfig {
  static constexpr uint32_t kPollIntervalMs = 20;  ///< Интервал опроса (50 Hz)
  static constexpr uint32_t kTimeoutMs = 100;  ///< Таймаут валидности сигнала
  static constexpr uint32_t kPulseMinUs =
      1000;  ///< Минимальная длительность импульса
  static constexpr uint32_t kPulseMaxUs =
      2000;  ///< Максимальная длительность импульса
  static constexpr uint32_t kPulseNeutralUs =
      1500;  ///< Нейтральная длительность
};

/**
 * @brief Конфигурация IMU
 */
struct ImuConfig {
  static constexpr uint32_t kReadIntervalMs = 2;  ///< Интервал чтения (500 Hz)
  static constexpr uint32_t kCalibSamples =
      1000;  ///< Количество семплов для калибровки gyro
  static constexpr uint32_t kCalibFullSamples =
      2000;  ///< Количество семплов для полной калибровки
  static constexpr float kGyroThreshold =
      0.5f;  ///< Порог движения гироскопа (рад/с)
  static constexpr float kAccelThreshold =
      0.1f;  ///< Порог движения акселерометра (g)
};

/**
 * @brief Конфигурация телеметрии
 */
struct TelemetryConfig {
  static constexpr uint32_t kSendIntervalMs =
      50;  ///< Интервал отправки (20 Hz)
  static constexpr size_t kJsonBufferSize = 1024;  ///< Размер буфера для JSON
};

/**
 * @brief Конфигурация failsafe
 */
struct FailsafeConfig {
  static constexpr uint32_t kTimeoutMs = 250;  ///< Таймаут потери управления
};

/**
 * @brief Конфигурация slew rate (ограничение скорости изменения)
 */
struct SlewRateConfig {
  static constexpr float kThrottleMaxPerSec =
      2.0f;  ///< Макс. изменение газа в секунду
  static constexpr float kSteeringMaxPerSec =
      4.0f;  ///< Макс. изменение руля в секунду
};

/**
 * @brief Конфигурация Wi-Fi команд
 */
struct WifiConfig {
  static constexpr uint32_t kCommandTimeoutMs =
      500;  ///< Таймаут актуальности команды
};

/**
 * @brief Конфигурация PWM
 */
struct PwmConfig {
  static constexpr uint32_t kUpdateIntervalMs =
      20;  ///< Интервал обновления PWM (50 Hz)
  static constexpr uint32_t kFrequencyHz =
      50;  ///< Частота PWM (стандарт для RC servo)
};

}  // namespace rc_vehicle::config