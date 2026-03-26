#pragma once

#include <cstdint>

#include "pid_controller.hpp"

namespace rc_vehicle {

/**
 * @brief Тип автоматического теста
 */
enum class TestType : uint8_t {
  Straight = 1,  ///< Прямолинейный проезд (фиксированный газ, steering=0)
  Circle = 2,    ///< Круговой проезд (фиксированный руль + PID по скорости)
  Step = 3,      ///< Step response (разгон, затем резкий поворот руля)
};

/**
 * @brief Параметры автоматического теста
 */
struct TestParams {
  TestType type{TestType::Straight};
  float target_accel_g{0.1f};   ///< Целевое ускорение при разгоне [0.02..0.3 g]
  float duration_sec{3.0f};     ///< Длительность основной фазы [1..30 с]
  float steering{0.0f};         ///< Руль для Circle [-1..1], Step target [-1..1]
};

/**
 * @brief Автоматические тестовые манёвры
 *
 * Управляет машиной по заданному паттерну для сбора данных:
 *
 * **Straight** (прямолинейный проезд):
 *   Accelerate → Cruise (steering=0, duration_sec) → Brake
 *   Для оценки drift гироскопа, EKF верификации
 *
 * **Circle** (круговой проезд):
 *   Accelerate → Cruise (steering=params.steering, duration_sec) → Brake
 *   Для калибровки CoM, оценки устойчивости
 *
 * **Step** (step response руля):
 *   Accelerate → Cruise (steering=0, 1с стабилизация) → Step (steering=target) → Brake
 *   Для оценки PID: overshoot, settling time
 *
 * Безопасность: RC override прерывает тест, failsafe → Stop().
 * Маркер test_marker проставляется в телеметрию.
 */
class TestRunner {
 public:
  enum class Phase {
    Idle,
    Accelerate,
    Cruise,       ///< Основная фаза (straight/circle) или стабилизация перед step
    StepExec,     ///< Только для Step: резкий поворот руля
    Brake,
    Done,
    Failed
  };

  struct Status {
    Phase phase{Phase::Idle};
    TestType type{TestType::Straight};
    float elapsed_sec{0.0f};       ///< Общее время теста
    float phase_elapsed_sec{0.0f}; ///< Время в текущей фазе
    bool valid{false};             ///< Тест завершён успешно
  };

  TestRunner() = default;

  /**
   * @brief Запустить тест
   * @param params Параметры теста
   * @return true при успешном запуске
   */
  bool Start(const TestParams& params);

  /** Прервать тест (failsafe / RC override). */
  void Stop();

  /** true пока тест активен (не Idle). */
  [[nodiscard]] bool IsActive() const {
    return phase_ != Phase::Idle && phase_ != Phase::Done &&
           phase_ != Phase::Failed;
  }

  /** true когда тест завершён (Done или Failed). */
  [[nodiscard]] bool IsFinished() const {
    return phase_ == Phase::Done || phase_ == Phase::Failed;
  }

  /** Текущая фаза. */
  [[nodiscard]] Phase GetPhase() const { return phase_; }

  /** Текущий статус. */
  [[nodiscard]] Status GetStatus() const;

  /** test_marker для телеметрии (0 если тест не активен). */
  [[nodiscard]] uint8_t GetTestMarker() const {
    return IsActive() ? static_cast<uint8_t>(type_) : 0;
  }

  /**
   * @brief Шаг теста (вызывается из control loop каждый тик)
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
  TestType type_{TestType::Straight};
  TestParams params_{};

  // PID для управления газом при разгоне
  PidController accel_pid_;

  // Время
  float total_elapsed_sec_{0.0f};
  float phase_elapsed_sec_{0.0f};
  float cruise_throttle_{0.0f};

  // Длительности фиксированных фаз
  static constexpr float kAccelDurationSec = 1.5f;
  static constexpr float kBrakeTimeoutSec = 3.0f;
  // Step: стабилизация перед поворотом
  static constexpr float kStepSettleSec = 1.0f;

  // ZUPT-пороги
  static constexpr float kStopAccelThresh = 0.05f;
  static constexpr float kStopGyroThresh = 3.0f;

  void TransitionTo(Phase next);
};

}  // namespace rc_vehicle
