#pragma once

#include "control_components.hpp"
#include "stabilization_config.hpp"
#include "vehicle_ekf.hpp"

namespace rc_vehicle {

/**
 * @brief Процессор детского режима (Kids Mode)
 *
 * Применяет ограничения газа, руля и slew rate,
 * а также усиленную защиту от заноса (anti-spin).
 *
 * Активность определяется через cfg_->mode == DriveMode::Kids —
 * отдельного трекинга current_mode_ нет. Control loop вызывает
 * Process() только когда ModeTraits.apply_input_limits == true,
 * но внутренняя проверка IsActive() остаётся как safety guard.
 */
class KidsModeProcessor {
 public:
  KidsModeProcessor() = default;

  /**
   * @brief Инициализация процессора
   * @param cfg Конфигурация стабилизации (содержит kids_mode и mode)
   * @param ekf EKF для получения угла заноса (anti-spin)
   * @param imu IMU handler (может быть nullptr если IMU не включён)
   */
  void Init(const StabilizationConfig& cfg, const VehicleEkf& ekf,
            const ImuHandler* imu);

  /**
   * @brief Применить ограничения Kids Mode
   * @param throttle Команда газа [in/out]
   * @param steering Команда руля [in/out]
   * @param dt_ms Шаг времени в миллисекундах
   * @param forward_accel Продольное ускорение IMU [g] для accel limiter
   */
  void Process(float& throttle, float& steering, uint32_t dt_ms,
               float forward_accel = 0.0f) noexcept;

  /**
   * @brief Проверить, активен ли Kids Mode
   * @return true если cfg_.mode == DriveMode::Kids
   */
  [[nodiscard]] bool IsActive() const noexcept {
    return cfg_ && cfg_->mode == DriveMode::Kids;
  }

  /**
   * @brief Проверить, сработала ли защита anti-spin
   * @return true если anti-spin активен
   */
  [[nodiscard]] bool IsAntiSpinActive() const noexcept {
    return anti_spin_active_;
  }

  /**
   * @brief Проверить, сработало ли ограничение по ускорению
   * @return true если accel limiter снижает throttle
   */
  [[nodiscard]] bool IsAccelLimitActive() const noexcept {
    return accel_limit_active_;
  }

  /**
   * @brief Сбросить состояние процессора
   */
  void Reset() noexcept;

 private:
  const StabilizationConfig* cfg_{nullptr};
  const VehicleEkf* ekf_{nullptr};
  const ImuHandler* imu_{nullptr};

  float smoothed_throttle_{0.0f};
  float smoothed_steering_{0.0f};
  bool anti_spin_active_{false};
  bool accel_limit_active_{false};
};

}  // namespace rc_vehicle
