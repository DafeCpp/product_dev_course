#pragma once

#include "control_components.hpp"
#include "madgwick_filter.hpp"
#include "pid_controller.hpp"
#include "stabilization_config.hpp"
#include "vehicle_ekf.hpp"

namespace rc_vehicle {

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
   * @param steering         Команда руля [in/out], корректируется в normal/sport
   * @param stab_w           Вес стабилизации [0..1]
   * @param mode_w           Вес перехода между режимами [0..1]
   * @param dt_ms            Шаг времени в миллисекундах
   */
  void Process(float& steering, float stab_w, float mode_w,
               uint32_t dt_ms) noexcept;

  /**
   * @brief Обновить PID-коэффициенты из конфигурации.
   * @param cfg Новая конфигурация
   */
  void SetGains(const StabilizationConfig& cfg) noexcept;

  /** @brief Сбросить интегратор и историю PID. */
  void Reset() noexcept { pid_.Reset(); }

  /** @brief Доступ к PID (для тестирования). */
  [[nodiscard]] const PidController& GetPid() const noexcept { return pid_; }

 private:
  const StabilizationConfig* cfg_{nullptr};
  const VehicleEkf* ekf_{nullptr};
  const ImuHandler* imu_{nullptr};
  PidController pid_;
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
   * @param cfg      Конфигурация стабилизации
   * @param madgwick Фильтр ориентации для получения pitch
   * @param imu      IMU handler (nullptr — компенсация не работает)
   */
  void Init(const StabilizationConfig& cfg, const MadgwickFilter& madgwick,
            const ImuHandler* imu);

  /**
   * @brief Применить pitch-компенсацию к газу.
   * @param throttle  Команда газа [in/out]
   * @param stab_w    Вес стабилизации [0..1]
   */
  void Process(float& throttle, float stab_w) noexcept;

 private:
  const StabilizationConfig* cfg_{nullptr};
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
   * @param throttle  Команда газа [in/out], корректируется в режиме drift
   * @param stab_w    Вес стабилизации [0..1]
   * @param mode_w    Вес перехода между режимами [0..1]
   * @param dt_ms     Шаг времени в миллисекундах
   */
  void Process(float& throttle, float stab_w, float mode_w,
               uint32_t dt_ms) noexcept;

  /**
   * @brief Обновить PID-коэффициенты из конфигурации.
   * @param cfg Новая конфигурация
   */
  void SetGains(const StabilizationConfig& cfg) noexcept;

  /** @brief Сбросить интегратор и историю PID. */
  void Reset() noexcept { pid_.Reset(); }

  /** @brief Доступ к PID (для тестирования). */
  [[nodiscard]] const PidController& GetPid() const noexcept { return pid_; }

 private:
  const StabilizationConfig* cfg_{nullptr};
  const VehicleEkf* ekf_{nullptr};
  const ImuHandler* imu_{nullptr};
  PidController pid_;
};

// ═════════════════════════════════════════════════════════════════════════════
// OversteerGuard
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Обнаружение заноса (oversteer prediction) и опциональное снижение газа.
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
   * @param cfg  Конфигурация стабилизации
   * @param ekf  EKF для получения угла заноса и его производной
   * @param imu  IMU handler (nullptr — guard не работает)
   */
  void Init(const StabilizationConfig& cfg, const VehicleEkf& ekf,
            const ImuHandler* imu);

  /**
   * @brief Один шаг oversteer detection.
   * @param throttle         Команда газа [in/out], может быть снижена при
   *                         oversteer
   * @param dt_ms            Шаг времени в миллисекундах
   * @param reduce_throttle  Разрешено ли снижение газа (определяется ModeTraits)
   */
  void Process(float& throttle, uint32_t dt_ms,
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
  const StabilizationConfig* cfg_{nullptr};
  const VehicleEkf* ekf_{nullptr};
  const ImuHandler* imu_{nullptr};

  float prev_slip_deg_{0.0f};   ///< Предыдущий угол заноса для оценки dslip/dt
  bool oversteer_active_{false}; ///< Текущее состояние oversteer detection
};

}  // namespace rc_vehicle
