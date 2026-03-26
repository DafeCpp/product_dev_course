#pragma once

#include <cstdint>

#include "stabilization_config.hpp"

namespace rc_vehicle {

/**
 * @brief Декларативное описание поведения режима вождения.
 *
 * Возвращается стратегией, используется в control loop
 * для выбора пути обработки без if/switch по DriveMode.
 * POD-тип, копируется на стеке — realtime-safe.
 */
struct ModeTraits {
  bool yaw_rate_active{true};
  bool pitch_comp_active{true};
  bool slip_angle_active{false};
  bool oversteer_guard_active{true};
  bool oversteer_reduces_throttle{true};
  bool use_slew_rate{true};
  bool apply_input_limits{false};
  bool anti_spin_active{false};
};

/**
 * @brief Интерфейс стратегии режима вождения.
 *
 * Каждый DriveMode реализует этот интерфейс.
 * Стратегии stateless — всё состояние остаётся в контроллерах и конфигурации.
 */
class IDriveModeStrategy {
 public:
  virtual ~IDriveModeStrategy() = default;

  /** @brief Идентификатор режима */
  [[nodiscard]] virtual DriveMode GetMode() const noexcept = 0;

  /** @brief Имя режима для логирования */
  [[nodiscard]] virtual const char* GetName() const noexcept = 0;

  /** @brief Описание поведения режима */
  [[nodiscard]] virtual ModeTraits GetTraits() const noexcept = 0;

  /**
   * @brief Применить PID-пресеты к конфигурации.
   * Вызывается при смене режима.
   */
  virtual void ApplyDefaults(StabilizationConfig& cfg) const noexcept = 0;
};

}  // namespace rc_vehicle
