#pragma once

#include <cstdint>
#include <firmware_common/pid_controller.hpp>

namespace rc_vehicle {

/**
 * @brief Фаза движения (разгон → круиз → торможение).
 */
enum class MotionPhase : uint8_t {
  Idle,        ///< Не запущен
  Settle,      ///< Замер baseline ускорения перед разгоном (throttle = 0)
  Accelerate,  ///< PID- или ramp-разгон
  Cruise,      ///< Постоянный газ; логика круиза у вызывающего
  Brake,       ///< Торможение (ZUPT-детекция остановки)
  Stopped,     ///< Остановка подтверждена
};

/**
 * @brief Универсальный компонент «разгон → круиз → торможение».
 *
 * Используется как член (композиция) в авто-процедурах:
 * SteeringTrimCalibration, TestRunner, SpeedCalibration,
 * ComOffsetCalibration, CalibrationManager (auto-forward).
 *
 * Вызывающий полностью владеет cruise-логикой (сбор данных, рулевое,
 * длительность) и вызывает EndCruise() когда готов перейти к торможению.
 *
 * Без виртуальных вызовов — подходит для control loop на 500 Гц.
 */
class MotionDriver {
 public:
  /** Способ набора скорости. */
  enum class AccelMode : uint8_t {
    Pid,         ///< PID по ускорению (target_value = target_accel_g)
    LinearRamp,  ///< Линейный рост throttle (target_value = target_throttle)
  };

  /** Конфигурация ZUPT (Zero-velocity Update) для детекции остановки. */
  struct ZuptConfig {
    float accel_thresh{0.05f};  ///< |accel_mag − 1g| < thresh
    float gyro_thresh{3.0f};    ///< |gyro_z| < thresh; 0 → без проверки гиро
  };

  /** Конфигурация фазы breakaway (только для AccelMode::Pid). */
  struct BreakawayConfig {
    float ramp_rate{0.5f};        ///< Рост throttle [1/с] в open-loop рампе
    float max_throttle{0.25f};    ///< Макс throttle при breakaway (fallback)
    float accel_thresh_g{0.03f};  ///< Порог детекции отрыва [g]
    int confirm_ticks{25};        ///< Подтверждение отрыва (~50мс при 500Hz)
  };

  /** Полная конфигурация одного прогона. */
  struct Config {
    AccelMode accel_mode{AccelMode::Pid};
    firmware_common::PidController::Gains pid_gains{0.3f, 0.2f, 0.0f, 0.15f,
                                                    0.5f};
    float target_value{0.1f};  ///< accel_g (Pid) или throttle (Ramp)
    float accel_duration_sec{1.5f};
    float min_effective_throttle{
        0.0f};                   ///< Только для LinearRamp; 0 → отключено
    float brake_throttle{0.0f};  ///< 0 = coast, <0 = reverse
    float brake_timeout_sec{3.0f};
    /// Тиков замера baseline перед разгоном (~100мс при 500Hz); 0 → без замера.
    /// Только для AccelMode::Pid — LinearRamp ускорение не использует.
    int settle_ticks{50};
    ZuptConfig zupt{};
    BreakawayConfig breakaway{};  ///< Только для AccelMode::Pid
  };

  MotionDriver() = default;

  /**
   * @brief Запустить прогон с заданной конфигурацией.
   *
   * Переходит в Settle (замер baseline ускорения), затем в Accelerate.
   * Полностью сбрасывает внутреннее состояние (включая PID-интегратор),
   * поэтому безопасно вызывать повторно (например, для второго прохода
   * ComOffsetCalibration).
   */
  void Start(const Config& config);

  /** Сбросить в Idle. */
  void Reset();

  /**
   * @brief Один тик (вызывать каждую итерацию control loop).
   *
   * В фазах Accelerate и Brake возвращает рассчитанный throttle.
   * В фазе Cruise возвращает cruise_throttle_ (зафиксированный
   * в конце разгона).
   * В Idle/Settle/Stopped возвращает 0.
   *
   * @param current_accel_g Продольное ускорение (g) — для PID
   * @param accel_magnitude Модуль полного ускорения (g) — для ZUPT
   * @param gyro_z_dps      Фильтрованный gyro Z (dps) — для ZUPT
   * @param dt_sec           Шаг времени (с)
   * @return throttle [-0.5 .. 0.5]
   */
  float Update(float current_accel_g, float accel_magnitude, float gyro_z_dps,
               float dt_sec);

  /**
   * @brief Сигнал окончания круиза → переход в Brake.
   *
   * Вызывается вызывающим когда cruise-данные собраны или
   * истекло время круиза. Если текущая фаза не Cruise — no-op.
   */
  void EndCruise();

  [[nodiscard]] MotionPhase GetPhase() const { return phase_; }
  [[nodiscard]] float GetCruiseThrottle() const { return cruise_throttle_; }
  [[nodiscard]] float GetPhaseElapsed() const { return phase_elapsed_sec_; }

  /** Baseline ускорения, замеренный в фазе Settle (g). */
  [[nodiscard]] float GetAccelBaseline() const { return baseline_accel_g_; }

 private:
  Config config_{};
  MotionPhase phase_{MotionPhase::Idle};
  firmware_common::PidController pid_;
  float phase_elapsed_sec_{0.0f};
  float cruise_throttle_{0.0f};

  // Baseline ускорения покоя (фаза Settle). Вычитается из current_accel_g,
  // чтобы статический офсет сигнала (наклон площадки, погрешность
  // accel_forward_vec) не давал ложный отрыв и не смещал уставку PI.
  float baseline_accel_g_{0.0f};
  double settle_sum_{0.0};
  int settle_count_{0};

  // Breakaway state (PID mode only)
  bool breakaway_detected_{false};
  float base_throttle_{0.0f};
  int breakaway_confirm_count_{0};

  float UpdateSettle(float current_accel_g);
  float UpdateAccelerate(float current_accel_g, float dt_sec);
  float UpdateBrake(float accel_magnitude, float gyro_z_dps);

  void TransitionTo(MotionPhase next);
};

}  // namespace rc_vehicle
