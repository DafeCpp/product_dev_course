#pragma once

#include <cstdint>

#include "pid_controller.hpp"

namespace rc_vehicle {

/**
 * @brief Калибровка смещения IMU относительно центра масс (Вариант B)
 *
 * Два круговых проезда (CW и CCW) с одинаковым |рулём|.
 * В установившемся режиме для каждого прохода собираются средние
 * значения ускорений (ax, ay) и угловой скорости (gz).
 *
 * Математика:
 *   a_imu = a_CoM + ω×(ω×r)
 *   В установившемся движении (α≈0):
 *     a_imu_x = a_CoM_x − ω²·rx
 *     a_imu_y = a_CoM_y − ω²·ry
 *
 *   Для CW и CCW при |ω_cw| ≈ |ω_ccw| = ω, a_CoM меняет знак:
 *     sum_ax = lin_ax_cw + lin_ax_ccw ≈ −(ω_cw² + ω_ccw²)·rx
 *     sum_ay = lin_ay_cw + lin_ay_ccw ≈ −(ω_cw² + ω_ccw²)·ry
 *
 *   Результат (в метрах):
 *     rx = −sum_ax · g / (ω_cw² + ω_ccw²)
 *     ry = −sum_ay · g / (ω_cw² + ω_ccw²)
 *
 * Фазы: Pass1 (CW): Accelerate→Cruise→Brake
 *        Pass2 (CCW): Accelerate→Cruise→Brake
 *        → Done / Failed
 *
 * Безопасность: RC override / failsafe → Stop().
 */
class ComOffsetCalibration {
 public:
  enum class Phase {
    Idle,
    Pass1_Accelerate,
    Pass1_Cruise,
    Pass1_Brake,
    Pass2_Accelerate,
    Pass2_Cruise,
    Pass2_Brake,
    Done,
    Failed
  };

  struct Result {
    float rx{0.0f};             ///< Смещение IMU→CoM вдоль оси X [м]
    float ry{0.0f};             ///< Смещение IMU→CoM вдоль оси Y [м]
    float omega_cw_dps{0.0f};   ///< Средняя угловая скорость CW [dps]
    float omega_ccw_dps{0.0f};  ///< Средняя угловая скорость CCW [dps]
    int samples_cw{0};
    int samples_ccw{0};
    bool valid{false};
  };

  ComOffsetCalibration() = default;

  /**
   * @brief Запустить калибровку
   * @param target_accel_g Целевое ускорение при разгоне [0.02..0.3 g]
   * @param steering_magnitude Абсолютное значение руля [0.1..1.0]
   * @param cruise_duration_sec Длительность круизной фазы [3..30 с]
   * @param gravity_vec Вектор гравитации в СК датчика (из ImuCalibData)
   * @return true при успешном запуске
   */
  bool Start(float target_accel_g = 0.1f, float steering_magnitude = 0.5f,
             float cruise_duration_sec = 5.0f,
             const float* gravity_vec = nullptr);

  /** Прервать калибровку. */
  void Stop();

  /** true пока калибровка активна. */
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

  /** Результат калибровки (валиден после Done). */
  [[nodiscard]] const Result& GetResult() const { return result_; }

  /**
   * @brief Шаг калибровки (вызывается из control loop)
   *
   * @param current_accel_g Продольное ускорение (g) для PID
   * @param accel_magnitude Модуль полного ускорения (g) для ZUPT
   * @param cal_ax Откалиброванный ax (g) — для сбора данных
   * @param cal_ay Откалиброванный ay (g) — для сбора данных
   * @param filtered_gz_dps Фильтрованный gyro Z (dps)
   * @param dt_sec Шаг времени (с)
   * @param[out] throttle Команда газа
   * @param[out] steering Команда руля
   */
  void Update(float current_accel_g, float accel_magnitude, float cal_ax,
              float cal_ay, float filtered_gz_dps, float dt_sec,
              float& throttle, float& steering);

  /** Сбросить в начальное состояние. */
  void Reset();

 private:
  Phase phase_{Phase::Idle};
  PidController accel_pid_;

  float target_accel_g_{0.1f};
  float steering_magnitude_{0.5f};
  float cruise_duration_sec_{5.0f};
  float gravity_vec_[3]{0.f, 0.f, 1.f};

  float phase_elapsed_sec_{0.0f};
  float cruise_throttle_{0.0f};

  // Аккумуляторы Pass 1 (CW: steering = +magnitude)
  double sum_ax_1_{0.0}, sum_ay_1_{0.0}, sum_gz_1_{0.0};
  int count_1_{0};

  // Аккумуляторы Pass 2 (CCW: steering = -magnitude)
  double sum_ax_2_{0.0}, sum_ay_2_{0.0}, sum_gz_2_{0.0};
  int count_2_{0};

  Result result_{};

  static constexpr float kAccelDurationSec = 1.5f;
  static constexpr float kBrakeTimeoutSec = 3.0f;
  static constexpr float kSettleSkipSec = 0.5f;  ///< Пропустить начало круиза
  static constexpr float kStopAccelThresh = 0.05f;
  static constexpr float kStopGyroThresh = 3.0f;
  static constexpr int kMinSamples = 500;
  static constexpr float kMinOmegaDps = 10.0f;  ///< Мин. ω для расчёта
  static constexpr float kMaxOffsetM = 0.3f;     ///< Макс. допустимый offset
  static constexpr float kGravity = 9.80665f;

  void TransitionTo(Phase next);
  void ComputeResult();
};

}  // namespace rc_vehicle
