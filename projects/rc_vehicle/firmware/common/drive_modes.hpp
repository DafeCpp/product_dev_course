#pragma once

#include "drive_mode_strategy.hpp"

namespace rc_vehicle {

/**
 * @brief Normal — базовый контроль рыскания, slip PID выключен.
 */
class NormalModeStrategy final : public IDriveModeStrategy {
 public:
  [[nodiscard]] DriveMode GetMode() const noexcept override {
    return DriveMode::Normal;
  }
  [[nodiscard]] const char* GetName() const noexcept override {
    return "Normal";
  }
  [[nodiscard]] ModeTraits GetTraits() const noexcept override {
    return {.yaw_rate_active = true,
            .pitch_comp_active = true,
            .slip_angle_active = false,
            .oversteer_guard_active = true,
            .oversteer_reduces_throttle = true,
            .use_slew_rate = true,
            .apply_input_limits = false,
            .anti_spin_active = false};
  }
  void ApplyDefaults(StabilizationConfig& cfg) const noexcept override;
};

/**
 * @brief Sport — агрессивные параметры, быстрый отклик, лёгкий slip assist.
 */
class SportModeStrategy final : public IDriveModeStrategy {
 public:
  [[nodiscard]] DriveMode GetMode() const noexcept override {
    return DriveMode::Sport;
  }
  [[nodiscard]] const char* GetName() const noexcept override {
    return "Sport";
  }
  [[nodiscard]] ModeTraits GetTraits() const noexcept override {
    return {.yaw_rate_active = true,
            .pitch_comp_active = true,
            .slip_angle_active = false,
            .oversteer_guard_active = true,
            .oversteer_reduces_throttle = true,
            .use_slew_rate = true,
            .apply_input_limits = false,
            .anti_spin_active = false};
  }
  void ApplyDefaults(StabilizationConfig& cfg) const noexcept override;
};

/**
 * @brief Drift — мягкая коррекция yaw, активный slip angle PID.
 */
class DriftModeStrategy final : public IDriveModeStrategy {
 public:
  [[nodiscard]] DriveMode GetMode() const noexcept override {
    return DriveMode::Drift;
  }
  [[nodiscard]] const char* GetName() const noexcept override {
    return "Drift";
  }
  [[nodiscard]] ModeTraits GetTraits() const noexcept override {
    return {.yaw_rate_active = false,
            .pitch_comp_active = true,
            .slip_angle_active = true,
            .oversteer_guard_active = true,
            .oversteer_reduces_throttle = false,
            .use_slew_rate = true,
            .apply_input_limits = false,
            .anti_spin_active = false};
  }
  void ApplyDefaults(StabilizationConfig& cfg) const noexcept override;
};

/**
 * @brief Kids — усиленная стабилизация, лимиты входов, anti-spin.
 */
class KidsModeStrategy final : public IDriveModeStrategy {
 public:
  [[nodiscard]] DriveMode GetMode() const noexcept override {
    return DriveMode::Kids;
  }
  [[nodiscard]] const char* GetName() const noexcept override { return "Kids"; }
  [[nodiscard]] ModeTraits GetTraits() const noexcept override {
    return {.yaw_rate_active = true,
            .pitch_comp_active = true,
            .slip_angle_active = false,
            .oversteer_guard_active = true,
            .oversteer_reduces_throttle = true,
            .use_slew_rate = true,
            .apply_input_limits = true,
            .anti_spin_active = true};
  }
  void ApplyDefaults(StabilizationConfig& cfg) const noexcept override;
};

/**
 * @brief DirectLaw — прямое управление без стабилизации и slew rate.
 */
class DirectLawStrategy final : public IDriveModeStrategy {
 public:
  [[nodiscard]] DriveMode GetMode() const noexcept override {
    return DriveMode::DirectLaw;
  }
  [[nodiscard]] const char* GetName() const noexcept override {
    return "DirectLaw";
  }
  [[nodiscard]] ModeTraits GetTraits() const noexcept override {
    return {.yaw_rate_active = false,
            .pitch_comp_active = false,
            .slip_angle_active = false,
            .oversteer_guard_active = false,
            .oversteer_reduces_throttle = false,
            .use_slew_rate = false,
            .apply_input_limits = false,
            .anti_spin_active = false};
  }
  void ApplyDefaults(StabilizationConfig& cfg) const noexcept override;
};

}  // namespace rc_vehicle
