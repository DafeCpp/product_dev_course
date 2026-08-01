#pragma once

#include <cstddef>
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
 * @brief Интервал отправки телеметрии по умолчанию, мс (50 мс = 20 Гц).
 *
 * Переопределяется на этапе сборки для отладочных сессий, напр. чтобы
 * нагрузить ingest/experiment-service потоком 100 Гц:
 *   idf.py build -DRC_TELEM_SEND_INTERVAL_MS=10
 * Флаг прокидывается в COMPILE_DEFINITIONS из esp32_s3/CMakeLists.txt
 * (через -DCMAKE_CXX_FLAGS ESP-IDF его теряет). Дефолт (без флага) —
 * штатные 20 Гц, поведение прошивки не меняется.
 */
#ifndef RC_TELEM_SEND_INTERVAL_MS
#define RC_TELEM_SEND_INTERVAL_MS 50
#endif

/**
 * @brief Конфигурация телеметрии
 */
struct TelemetryConfig {
  static constexpr uint32_t kSendIntervalMs =
      RC_TELEM_SEND_INTERVAL_MS;  ///< Интервал отправки (дефолт 50 мс = 20 Гц)
  static constexpr size_t kJsonBufferSize = 1024;  ///< Размер буфера для JSON
};

/**
 * @brief Конфигурация кольцевого буфера телеметрии
 */
struct TelemetryLogConfig {
  static constexpr uint32_t kLogIntervalMs =
      10;  ///< Интервал записи в лог (100 Hz)
  // 52k frames keep about 8.7 minutes at 100 Hz and reserve 1 MB PSRAM for
  // compact stabilization-configuration deltas.
  static constexpr size_t kCapacityFrames = 52000;
  static constexpr size_t kMaxExportFrames =
      200;  ///< Макс. кадров для экспорта
};

/**
 * @brief Конфигурация Low-Pass фильтра
 */
struct LpfConfig {
  static constexpr float kDefaultCutoffHz =
      30.0f;                                     ///< Частота среза по умолчанию
  static constexpr float kMinCutoffHz = 5.0f;    ///< Минимальная частота среза
  static constexpr float kMaxCutoffHz = 100.0f;  ///< Максимальная частота среза
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
      0.5f;  ///< Макс. изменение газа в секунду
  static constexpr float kSteeringMaxPerSec =
      1.0f;  ///< Макс. изменение руля в секунду
};

/**
 * @brief Конфигурация диагностического вывода
 */
struct DiagnosticsConfig {
  static constexpr uint32_t kIntervalMs =
      5000;  ///< Интервал вывода диагностики
};

#ifdef RC_PROFILE_LOOP
/**
 * @brief Порог "outlier"-итерации для профилировщика control loop (LOS-219).
 * 2x номинальный бюджет цикла (ControlLoopConfig::kPeriodMs).
 */
struct ProfilingConfig {
  static constexpr uint32_t kOutlierThresholdUs =
      ControlLoopConfig::kPeriodMs * 1000 * 2;  ///< 4000 мкс
};
#endif

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

/**
 * @brief Конфигурация UDP-стриминга телеметрии
 */
struct UdpTelemConfig {
  static constexpr uint16_t kControlPort =
      5556;  ///< Порт приёма команд START/STOP/STATUS
  static constexpr uint16_t kDefaultDataPort =
      5555;  ///< Порт отправки телеметрии (по умолчанию)
  static constexpr size_t kQueueDepth =
      64;  ///< Глубина FreeRTOS очереди кадров
  static constexpr size_t kSenderTaskStack = 4096;  ///< Стек задачи отправки
  static constexpr uint8_t kSenderTaskPriority =
      3;  ///< Приоритет задачи отправки
  static constexpr uint8_t kControlTaskPriority =
      2;  ///< Приоритет задачи приёма команд
  static constexpr size_t kControlTaskStack =
      4096;                                   ///< Стек задачи приёма команд
  static constexpr uint8_t kDefaultHz = 100;  ///< Частота отправки по умолчанию
  static constexpr size_t kMaxCommandLen = 64;  ///< Макс. длина UDP-команды
  static constexpr uint8_t kPacketVersion = 1;  ///< Версия протокола пакета
};

}  // namespace rc_vehicle::config
