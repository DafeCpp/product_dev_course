#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace rc_vehicle {

/**
 * @brief Тип события телеметрии (старт/стоп режима или калибровки).
 *
 * Только переходы записываются — не непрерывный поток.
 * Это позволяет найти в логе временны́е рамки каждого режима
 * без дополнительных полей в каждом кадре TelemetryLogFrame.
 */
enum class TelemetryEventType : uint8_t {
  // ── IMU-калибровка (CalibrationManager) ──────────────────────────────
  ImuCalibStart  = 1,  ///< param: 0=gyro_only  1=full  2=auto_forward(stage2)
  ImuCalibDone   = 2,  ///< param: stage (1 или 2)
  ImuCalibFailed = 3,  ///< param: stage

  // ── Калибровка trim руля ──────────────────────────────────────────────
  TrimCalibStart  = 4,
  TrimCalibDone   = 5,
  TrimCalibFailed = 6,

  // ── Калибровка CoM offset ─────────────────────────────────────────────
  ComCalibStart  = 7,
  ComCalibDone   = 8,
  ComCalibFailed = 9,

  // ── Калибровка скорости ───────────────────────────────────────────────
  SpeedCalibStart  = 10,
  SpeedCalibDone   = 11,
  SpeedCalibFailed = 12,

  // ── Тестовые манёвры (TestRunner) ─────────────────────────────────────
  TestStart   = 13,  ///< param: TestType (1=Straight 2=Circle 3=Step)
  TestDone    = 14,  ///< param: TestType
  TestFailed  = 15,  ///< param: TestType
  TestStopped = 16,  ///< param: TestType

  // ── Калибровка магнитометра ───────────────────────────────────────────
  MagCalibStart     = 17,
  MagCalibDone      = 18,
  MagCalibFailed    = 19,
  MagCalibCancelled = 20,
};

/**
 * @brief Одно событие телеметрии (16 байт).
 *
 * Записывается только при изменении состояния (старт или стоп).
 *
 * Семантика value1/value2 по типу события (только Start-события):
 *   TestStart:        value1 = duration_sec,     value2 = steering [-1..1]
 *   TrimCalibStart:   value1 = target_accel_g,   value2 = 0
 *   ComCalibStart:    value1 = target_accel_g,   value2 = steering_magnitude
 *   SpeedCalibStart:  value1 = target_throttle,  value2 = cruise_duration_sec
 *   ImuCalibStart:    value1 = target_accel_g (auto_forward), value2 = 0
 */
struct TelemetryEvent {
  uint32_t           ts_ms{0};    ///< Метка времени события [мс]
  TelemetryEventType type{};      ///< Тип события
  uint8_t            param{0};    ///< Доп. параметр (TestType, CalibMode и т.п.)
  uint8_t            _pad[2]{};   ///< Выравнивание
  float              value1{0.0f}; ///< Первичный параметр режима
  float              value2{0.0f}; ///< Вторичный параметр режима
};
static_assert(sizeof(TelemetryEvent) == 16, "TelemetryEvent size mismatch");

/**
 * @brief Потокобезопасный кольцевой буфер событий телеметрии.
 *
 * Фиксированная ёмкость kCapacity = 256 (2 КБ) — хранится в обычной heap,
 * PSRAM не нужен.
 *
 * Push() вытесняет самое старое событие при переполнении.
 * Чтение: GetEvent(0) = самое старое, GetEvent(Count()-1) = самое новое.
 */
class TelemetryEventLog {
 public:
  static constexpr size_t kCapacity = 256;

  TelemetryEventLog();
  ~TelemetryEventLog();

  TelemetryEventLog(const TelemetryEventLog&)            = delete;
  TelemetryEventLog& operator=(const TelemetryEventLog&) = delete;

  /** Записать событие (вытесняет старые при переполнении). */
  void Push(const TelemetryEvent& evt);

  /** Количество записанных событий. */
  [[nodiscard]] size_t Count() const;

  /** Ёмкость буфера. */
  [[nodiscard]] constexpr size_t Capacity() const { return kCapacity; }

  /**
   * Получить событие по индексу (0 = oldest, Count()-1 = newest).
   * @return true если idx < Count()
   */
  bool GetEvent(size_t idx, TelemetryEvent& out) const;

  /** Очистить буфер. */
  void Clear();

 private:
  TelemetryEvent  buf_[kCapacity]{};
  size_t          write_pos_{0};
  size_t          count_{0};
  mutable std::mutex mutex_;
};

}  // namespace rc_vehicle
