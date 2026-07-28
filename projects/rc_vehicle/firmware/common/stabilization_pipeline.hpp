#pragma once

#include <firmware_common/pid_controller.hpp>

#include "control_components.hpp"
#include "control_tick.hpp"
#include "drive_mode_strategy.hpp"
#include "kids_mode_processor.hpp"
#include "madgwick_filter.hpp"
#include "stabilization_config.hpp"
#include "vehicle_ekf.hpp"

namespace rc_vehicle {

/** Snapshot consumed by the stabilization pipeline for one control tick. */
struct StabilizationInput {
  ControlSetpoint command{};
  uint32_t dt_ms{0};
  float speed_ms{0.0f};
  float slip_angle_deg{0.0f};
  float yaw_rate_rps{0.0f};
  float vx_variance{0.0f};
  float filtered_gyro_z_dps{0.0f};
  float pitch_deg{0.0f};
  float forward_accel_g{0.0f};
  float stabilization_weight{0.0f};
  float mode_transition_weight{0.0f};
  bool imu_enabled{false};
  bool ekf_diverged{false};
};

/** Command and motor-model target produced by stabilization for this tick. */
struct StabilizationOutput {
  ControlSetpoint command{};
  float motor_model_target_throttle{0.0f};
};

// ═════════════════════════════════════════════════════════════════════════════
// YawRateController
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief ПИД-регулятор угловой скорости рыскания (yaw rate) с адаптивным
 *        масштабированием по скорости из EKF.
 *
 * Активен в режимах Normal (0) и Sport (1). В Drift mode (2) yaw PID
 * отключён — управление рулём остаётся за водителем, а стабилизацией
 * заноса занимается SlipAngleController.
 *
 * Извлечён из VehicleControlUnified::ControlTaskLoop() (строки 154–173).
 */
class YawRateController {
 public:
  YawRateController() = default;

  /**
   * @brief Инициализация: привязать зависимости и установить начальные PID
   *        коэффициенты из конфигурации.
   * @param cfg  Конфигурация стабилизации (хранится по ссылке — читается live)
   * @param ekf  EKF для оценки скорости (адаптивный PID)
   * @param imu  IMU handler (может быть nullptr если IMU не включён)
   */
  void Init(const StabilizationConfig& cfg, const VehicleEkf& ekf,
            const ImuHandler* imu);

  /**
   * @brief Один шаг yaw rate PID.
   * @param cfg              Живой снимок конфига текущей итерации (FW-R23)
   * @param steering         Команда руля [in/out], корректируется в
   * normal/sport
   * @param stab_w           Вес стабилизации [0..1]
   * @param mode_w           Вес перехода между режимами [0..1]
   * @param dt_ms            Шаг времени в миллисекундах
   * @param reversing        true — машина едет назад (команда газа < 0):
   *                         yaw-rate стабилизация отключается (FW-R22, иначе
   *                         автоколебания руля)
   */
  void Process(const StabilizationConfig& cfg, float& steering, float stab_w,
               float mode_w, uint32_t dt_ms, bool reversing = false) noexcept;

  /** Snapshot-based production path. */
  void Process(const StabilizationConfig& cfg, float& steering,
               const StabilizationInput& input,
               bool reversing = false) noexcept;

  /**
   * @brief Обновить PID-коэффициенты из конфигурации.
   * @param cfg Новая конфигурация
   */
  void SetGains(const StabilizationConfig& cfg) noexcept;

  /** @brief Сбросить интегратор и историю PID. */
  void Reset() noexcept { pid_.Reset(); }

  /** @brief Доступ к PID (для тестирования). */
  [[nodiscard]] const firmware_common::PidController& GetPid() const noexcept {
    return pid_;
  }

 private:
  const VehicleEkf* ekf_{nullptr};
  const ImuHandler* imu_{nullptr};
  firmware_common::PidController pid_;
};

// ═════════════════════════════════════════════════════════════════════════════
// PitchCompensator
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Компенсация наклона: коррекция газа по углу pitch (стабилизация на
 *        склонах).
 *
 * Положительный pitch (нос вверх) → увеличить газ.
 * Отрицательный pitch (нос вниз) → уменьшить газ.
 * Коррекция ограничена pitch_comp_max_correction.
 *
 * Извлечён из VehicleControlUnified::ControlTaskLoop() (строки 180–191).
 * Fix #8 (REFACTORING.md): ручной if/else заменён на std::clamp.
 */
class PitchCompensator {
 public:
  PitchCompensator() = default;

  /**
   * @brief Инициализация: привязать зависимости.
   * @param madgwick Фильтр ориентации для получения pitch
   * @param imu      IMU handler (nullptr — компенсация не работает)
   */
  void Init(const MadgwickFilter& madgwick, const ImuHandler* imu);

  /**
   * @brief Применить pitch-компенсацию к газу.
   * @param cfg       Живой снимок конфига текущей итерации (FW-R23)
   * @param throttle  Команда газа [in/out]
   * @param stab_w    Вес стабилизации [0..1]
   */
  void Process(const StabilizationConfig& cfg, float& throttle,
               float stab_w) noexcept;

  /** Snapshot-based production path. */
  void Process(const StabilizationConfig& cfg, float& throttle,
               const StabilizationInput& input) noexcept;

 private:
  const MadgwickFilter* madgwick_{nullptr};
  const ImuHandler* imu_{nullptr};
};

// ═════════════════════════════════════════════════════════════════════════════
// SlipAngleController
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief ПИД-регулятор угла заноса (slip angle) для поддержки дрифта.
 *
 * Активен только в Drift mode (mode=2) при включённом IMU.
 * Корректирует газ для поддержания целевого угла заноса из EKF.
 *
 * Извлечён из VehicleControlUnified::ControlTaskLoop() (строки 197–207).
 */
class SlipAngleController {
 public:
  SlipAngleController() = default;

  /**
   * @brief Инициализация: привязать зависимости и установить начальные PID
   *        коэффициенты из конфигурации.
   * @param cfg  Конфигурация стабилизации
   * @param ekf  EKF для получения текущего угла заноса
   * @param imu  IMU handler (nullptr — PID не работает)
   */
  void Init(const StabilizationConfig& cfg, const VehicleEkf& ekf,
            const ImuHandler* imu);

  /**
   * @brief Один шаг slip angle PID (только в drift mode).
   * @param cfg       Живой снимок конфига текущей итерации (FW-R23)
   * @param throttle  Команда газа [in/out], корректируется в режиме drift
   * @param stab_w    Вес стабилизации [0..1]
   * @param mode_w    Вес перехода между режимами [0..1]
   * @param dt_ms     Шаг времени в миллисекундах
   */
  void Process(const StabilizationConfig& cfg, float& throttle, float stab_w,
               float mode_w, uint32_t dt_ms) noexcept;

  /** Snapshot-based production path. */
  void Process(const StabilizationConfig& cfg, float& throttle,
               const StabilizationInput& input) noexcept;

  /**
   * @brief Обновить PID-коэффициенты из конфигурации.
   * @param cfg Новая конфигурация
   */
  void SetGains(const StabilizationConfig& cfg) noexcept;

  /** @brief Сбросить интегратор и историю PID. */
  void Reset() noexcept { pid_.Reset(); }

  /** @brief Доступ к PID (для тестирования). */
  [[nodiscard]] const firmware_common::PidController& GetPid() const noexcept {
    return pid_;
  }

 private:
  const VehicleEkf* ekf_{nullptr};
  const ImuHandler* imu_{nullptr};
  firmware_common::PidController pid_;
};

// ═════════════════════════════════════════════════════════════════════════════
// OversteerGuard
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Обнаружение заноса (oversteer prediction) и опциональное снижение
 * газа.
 *
 * Срабатывает когда |slip_angle| > thresh_slip И |d(slip)/dt| > thresh_rate.
 * В режимах Normal/Sport снижает газ на oversteer_throttle_reduction.
 * В Drift mode снижение газа отключено (занос ожидается и желателен).
 *
 * Извлечён из VehicleControlUnified::ControlTaskLoop() (строки 213–227).
 * Владеет prev_slip_deg_ и oversteer_active_ (перенесены из VCU).
 */
class OversteerGuard {
 public:
  OversteerGuard() = default;

  /**
   * @brief Инициализация: привязать зависимости.
   * @param ekf  EKF для получения угла заноса и его производной
   * @param imu  IMU handler (nullptr — guard не работает)
   */
  void Init(const VehicleEkf& ekf, const ImuHandler* imu);

  /**
   * @brief Один шаг oversteer detection.
   * @param cfg              Живой снимок конфига текущей итерации (FW-R23)
   * @param throttle         Команда газа [in/out], может быть снижена при
   *                         oversteer
   * @param dt_ms            Шаг времени в миллисекундах
   * @param reduce_throttle  Разрешено ли снижение газа (определяется
   * ModeTraits)
   */
  void Process(const StabilizationConfig& cfg, float& throttle, uint32_t dt_ms,
               bool reduce_throttle = true) noexcept;

  /** Snapshot-based production path. */
  void Process(const StabilizationConfig& cfg, float& throttle,
               const StabilizationInput& input,
               bool reduce_throttle = true) noexcept;

  /** @brief Сбросить состояние (вызывается при failsafe). */
  void Reset() noexcept;

  /** @brief Текущий флаг срабатывания oversteer. */
  [[nodiscard]] bool IsActive() const noexcept { return oversteer_active_; }

  /**
   * @brief Указатель на флаг oversteer для TelemetryHandler::SetOversteerWarn.
   * @return Указатель на oversteer_active_
   */
  [[nodiscard]] const bool* GetActivePtr() const noexcept {
    return &oversteer_active_;
  }

 private:
  const VehicleEkf* ekf_{nullptr};
  const ImuHandler* imu_{nullptr};

  float prev_slip_deg_{0.0f};  ///< Предыдущий угол заноса для оценки dslip/dt
  bool oversteer_active_{false};  ///< Текущее состояние oversteer detection
};

/**
 * Ordered, mode-agnostic stabilization computation.
 *
 * Drive-mode selection happens outside this class. The pipeline receives only
 * declarative ModeTraits and value snapshots; it performs no platform I/O.
 */
class StabilizationPipeline {
 public:
  StabilizationPipeline(YawRateController& yaw_ctrl,
                        PitchCompensator& pitch_ctrl,
                        SlipAngleController& slip_ctrl,
                        OversteerGuard& oversteer_guard,
                        KidsModeProcessor& kids_processor) noexcept
      : yaw_ctrl_(yaw_ctrl),
        pitch_ctrl_(pitch_ctrl),
        slip_ctrl_(slip_ctrl),
        oversteer_guard_(oversteer_guard),
        kids_processor_(kids_processor) {}

  [[nodiscard]] StabilizationOutput Process(
      const StabilizationConfig& cfg, const ModeTraits& policy,
      const StabilizationInput& input) noexcept;

 private:
  YawRateController& yaw_ctrl_;
  PitchCompensator& pitch_ctrl_;
  SlipAngleController& slip_ctrl_;
  OversteerGuard& oversteer_guard_;
  KidsModeProcessor& kids_processor_;
};

}  // namespace rc_vehicle
