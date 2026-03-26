#pragma once

#include <cstdint>

namespace rc_vehicle {

/**
 * @brief Автоматическая калибровка скорости (throttle → speed gain)
 *
 * Прямолинейный прогон с фиксированным газом. Алгоритм:
 * 1. Разгон: плавный рост throttle до target_throttle за kAccelSec
 * 2. Круиз: сбор EKF speed_ms в течение kCruiseSec
 * 3. Торможение: обратный газ до остановки (ZUPT по ускорению)
 * 4. Результат: speed_gain = mean_speed / target_throttle [m/s per unit]
 *
 * RC-пульт имеет приоритет (безопасность).
 */
class SpeedCalibration {
 public:
  enum class Phase { Idle, Accelerate, Cruise, Brake, Done, Failed };

  struct Result {
    float target_throttle{0.0f};    ///< Газ при калибровке
    float mean_speed_ms{0.0f};      ///< Средняя скорость за круиз [m/s]
    float speed_gain{0.0f};         ///< [m/s] на единицу throttle
    int samples{0};                 ///< Количество семплов круиза
    bool valid{false};
  };

  SpeedCalibration() = default;

  /**
   * @param target_throttle Газ при калибровке [0.1..0.8]
   * @param cruise_duration_sec Длительность сбора данных [1..10]
   */
  bool Start(float target_throttle = 0.3f, float cruise_duration_sec = 3.0f);

  void Stop();

  [[nodiscard]] bool IsActive() const {
    return phase_ != Phase::Idle && phase_ != Phase::Done &&
           phase_ != Phase::Failed;
  }
  [[nodiscard]] bool IsFinished() const {
    return phase_ == Phase::Done || phase_ == Phase::Failed;
  }
  [[nodiscard]] Phase GetPhase() const { return phase_; }
  [[nodiscard]] const Result& GetResult() const { return result_; }

  /**
   * @param speed_ms  EKF speed [m/s]
   * @param accel_mag Модуль полного ускорения [g] — для ZUPT
   * @param dt_sec    Шаг времени [с]
   * @param[out] throttle, steering команды
   */
  void Update(float speed_ms, float accel_mag, float dt_sec, float& throttle,
              float& steering);

  void Reset();

 private:
  Phase phase_{Phase::Idle};
  Result result_{};

  float target_throttle_{0.3f};
  float cruise_duration_sec_{3.0f};

  float phase_elapsed_sec_{0.0f};
  float cruise_throttle_{0.0f};  // throttle зафиксированный в конце разгона

  double speed_sum_{0.0};
  int speed_count_{0};

  static constexpr float kAccelSec = 1.5f;       // длительность разгона
  static constexpr float kBrakeTimeoutSec = 3.0f;
  static constexpr float kBrakeThrottle = -0.4f; // обратный газ при торможении
  static constexpr float kStopAccelThresh = 0.05f; // порог остановки [g]
  static constexpr int kMinSamples = 200;

  void ComputeResult();
};

}  // namespace rc_vehicle
