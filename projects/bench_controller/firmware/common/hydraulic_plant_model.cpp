#include "hydraulic_plant_model.hpp"

#include <algorithm>

namespace bench {

HydraulicPlantModel::State HydraulicPlantModel::Step(
    float valve_cmd, float dt_sec) noexcept {
  valve_cmd = std::clamp(valve_cmd, -1.0f, 1.0f);

  // Золотник: апериодическое звено 1-го порядка.
  if (config_.spool_tau_s > 0.0f) {
    const float alpha = std::min(dt_sec / config_.spool_tau_s, 1.0f);
    state_.spool += (valve_cmd - state_.spool) * alpha;
  } else {
    state_.spool = valve_cmd;
  }

  // Расход ∝ золотнику → скорость поршня → перемещение.
  const float velocity_mm_s = config_.piston_speed_mm_s * state_.spool;
  state_.position_mm = std::clamp(
      state_.position_mm + velocity_mm_s * dt_sec,
      -config_.position_limit_mm, config_.position_limit_mm);

  // Образец как пружина; при разрушении жёсткость — остаточная доля.
  state_.force_n =
      config_.stiffness_n_mm * stiffness_scale_ * state_.position_mm;
  return state_;
}

}  // namespace bench
