#pragma once

#include <cstdint>

#include "control_components.hpp"
#include "stabilization_config.hpp"
#include "vehicle_state_estimator.hpp"

namespace rc_vehicle {

/** Mutable control setpoint passed between control-loop stages. */
struct ControlSetpoint {
  float throttle{0.0f};
  float steering{0.0f};
};

/**
 * Value snapshots shared by all stages of one 500 Hz control tick.
 *
 * The processor assembles this object before state estimation. Estimation may
 * apply CoM correction to sensors; all later stages receive it as const.
 */
struct ControlTickInput {
  uint32_t now_ms{0};
  uint32_t dt_ms{0};
  StabilizationConfig config{};
  SensorSnapshot sensors{};
};

/** Values produced and refined by the ordered stages of one control tick. */
struct ControlTickState {
  VehicleStateEstimate estimate{};
  ControlSetpoint command{};
  float motor_model_target_throttle{0.0f};
  bool failsafe_active{false};
};

}  // namespace rc_vehicle
