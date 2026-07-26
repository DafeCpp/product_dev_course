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
 * Конфиг передаётся в Process()/IsActive() аргументом — живой per-tick снимок
 * из control loop (FW-R21). Процессор НЕ хранит указатель на конфиг: при старте
 * режим обычно ещё не Kids, а переключение режима/пресета происходит в
 * рантайме, поэтому единственный корректный источник — снимок текущей итерации.
 * Control loop вызывает Process() только когда ModeTraits.apply_input_limits ==
 * true, но внутренняя проверка IsActive(cfg) остаётся как safety guard.
 */
class KidsModeProcessor {
 public:
  KidsModeProcessor() = default;

  /**
   * @brief Инициализация процессора
   * @param ekf EKF для получения угла заноса (anti-spin) и скорости
   * @param imu IMU handler (может быть nullptr если IMU не включён)
   */
  void Init(const VehicleEkf& ekf, const ImuHandler* imu);

  /**
   * @brief Применить ограничения Kids Mode
   * @param cfg Живой снимок конфига стабилизации текущей итерации
   * @param throttle Команда газа [in/out]
   * @param steering Команда руля [in/out]
   * @param dt_ms Шаг времени в миллисекундах
   * @param forward_accel Продольное ускорение IMU [g] для accel limiter
   * @param throttle_before_speed_limit Не-null: получает throttle после
   *        обычных Kids-ограничений, но до speed limiter (LOS-246)
   * @param apply_speed_limit Применить speed limiter сразу; control loop
   *        передаёт false, чтобы применить его после остальных стабилизаторов
   */
  void Process(const StabilizationConfig& cfg, float& throttle, float& steering,
               uint32_t dt_ms, float forward_accel = 0.0f,
               float* throttle_before_speed_limit = nullptr,
               bool apply_speed_limit = true) noexcept;

  /**
   * @brief Применить feedback speed limiter и slew фактической команды.
   * @param dt_ms Ненулевой шаг применяет Kids throttle slew после limiter.
   */
  void ApplySpeedLimit(const StabilizationConfig& cfg, float& throttle,
                       uint32_t dt_ms = 0) noexcept;

  /**
   * @brief Проверить, активен ли Kids Mode для переданного конфига
   * @return true если cfg.mode == DriveMode::Kids
   */
  [[nodiscard]] bool IsActive(const StabilizationConfig& cfg) const noexcept {
    return cfg.mode == DriveMode::Kids;
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
   * @brief Проверить, сработало ли ограничение по скорости (EKF)
   * @return true если speed limiter снижает throttle
   */
  [[nodiscard]] bool IsSpeedLimitActive() const noexcept {
    return speed_limit_active_;
  }

  /**
   * @brief Сбросить состояние процессора
   */
  void Reset() noexcept;

 private:
  const VehicleEkf* ekf_{nullptr};
  const ImuHandler* imu_{nullptr};

  float smoothed_throttle_{0.0f};
  // Независимая pre-speed-limit ветка для motor-model snapshot (LOS-246).
  float counterfactual_throttle_{0.0f};
  float smoothed_steering_{0.0f};
  bool anti_spin_active_{false};
  bool accel_limit_active_{false};
  bool speed_limit_active_{false};
};

}  // namespace rc_vehicle
