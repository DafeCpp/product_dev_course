#pragma once

#include "pid_controller.hpp"

namespace rc_vehicle {

/**
 * @brief Автоматическая калибровка steering trim
 *
 * Определяет механическое смещение нейтрали руля. Алгоритм:
 * 1. Разгон до целевого ускорения (PID по продольному ускорению)
 * 2. Круиз с steering=0: сбор filtered_gyro_z за несколько секунд
 * 3. Торможение, расчёт trim из среднего yaw_rate:
 *    trim_correction = -mean_yaw_rate / steer_to_yaw_rate_dps
 * 4. Обновление StabilizationConfig.steering_trim
 *
 * RC-пульт имеет приоритет (безопасность): при активном RC авто-движение
 * не применяется. При failsafe — немедленная остановка.
 */
class SteeringTrimCalibration {
 public:
  enum class Phase { Idle, Accelerate, Cruise, Brake, Done, Failed };

  /** Результат калибровки. */
  struct Result {
    float trim{0.0f};           ///< Рассчитанный trim
    float mean_yaw_rate{0.0f};  ///< Средний yaw rate при cruise (dps)
    int samples{0};             ///< Количество собранных семплов
    bool valid{false};          ///< Калибровка успешна
  };

  SteeringTrimCalibration() = default;

  /**
   * @brief Запустить калибровку trim руля
   * @param target_accel_g Целевое ускорение при разгоне [0.02..0.3 g]
   * @param current_trim Текущее значение trim (для коррекции)
   * @param steer_to_yaw_rate_dps Чувствительность руля (dps при steering=1.0)
   * @return true при успешном запуске
   */
  bool Start(float target_accel_g = 0.1f, float current_trim = 0.0f,
             float steer_to_yaw_rate_dps = 180.0f);

  /** Прервать калибровку (вызывается из failsafe). */
  void Stop();

  /** true пока идёт авто-движение для калибровки trim. */
  [[nodiscard]] bool IsActive() const {
    return phase_ != Phase::Idle && phase_ != Phase::Done &&
           phase_ != Phase::Failed;
  }

  /** true когда калибровка завершена (Done или Failed). */
  [[nodiscard]] bool IsFinished() const {
    return phase_ == Phase::Done || phase_ == Phase::Failed;
  }

  /** Текущая фаза. */
  [[nodiscard]] Phase GetPhase() const { return phase_; }

  /** Результат калибровки (валиден после Phase::Done). */
  [[nodiscard]] const Result& GetResult() const { return result_; }

  /**
   * @brief Шаг калибровки (вызывается из control loop каждый тик)
   *
   * @param current_accel_g Текущее продольное ускорение (g)
   * @param accel_magnitude Модуль полного ускорения (g), для ZUPT
   * @param filtered_gz_dps Фильтрованный gyro Z (dps)
   * @param dt_sec Шаг времени (с)
   * @param[out] throttle Команда газа
   * @param[out] steering Команда руля
   */
  void Update(float current_accel_g, float accel_magnitude,
              float filtered_gz_dps, float dt_sec, float& throttle,
              float& steering);

  /** Сбросить в начальное состояние. */
  void Reset();

 private:
  Phase phase_{Phase::Idle};
  Result result_{};

  // Параметры
  float target_accel_g_{0.1f};
  float current_trim_{0.0f};
  float steer_to_yaw_rate_dps_{180.0f};

  // PID для управления газом
  PidController accel_pid_;

  // Фазовое время
  float phase_elapsed_sec_{0.0f};
  float cruise_throttle_{0.0f};

  // Сбор yaw_rate во время круиза
  double yaw_rate_sum_{0.0};
  int yaw_rate_count_{0};

  // Длительности фаз
  static constexpr float kAccelDurationSec = 1.5f;
  static constexpr float kCruiseDurationSec = 4.0f;  // Дольше чем auto_forward
  static constexpr float kBrakeTimeoutSec = 3.0f;

  // ZUPT-пороги
  static constexpr float kStopAccelThresh = 0.05f;
  static constexpr float kStopGyroThresh = 3.0f;

  // Минимальное количество семплов для валидного результата
  static constexpr int kMinSamples = 500;

  // Максимальный допустимый yaw_rate — если больше, trim не поможет
  static constexpr float kMaxYawRateDps = 30.0f;

  void ComputeResult();
};

}  // namespace rc_vehicle
