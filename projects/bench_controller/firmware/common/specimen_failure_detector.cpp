#include "specimen_failure_detector.hpp"

#include <cmath>

namespace bench {

bool SpecimenFailureDetector::Update(float force_actual_n,
                                     float force_setpoint_n) noexcept {
  if (latched_) {
    return true;
  }

  const float sp_abs = std::fabs(force_setpoint_n);
  if (sp_abs < config_.min_force_n) {
    // Уставка мала (проход через ноль) — тик не информативен:
    // счётчик замораживаем, чтобы разрушение на синусе не «забывалось».
    return false;
  }

  // Сила должна совпадать с уставкой по знаку и величине; разрыв
  // образца проявляется как |actual| << |setpoint|.
  const float actual_along_sp =
      force_setpoint_n >= 0.0f ? force_actual_n : -force_actual_n;
  const float threshold = (1.0f - config_.drop_fraction) * sp_abs;
  const bool in_track = actual_along_sp >= threshold;

  if (!armed_) {
    // Взведение: ждём выхода на режим (устойчивое слежение).
    in_track_count_ = in_track ? in_track_count_ + 1 : 0;
    if (in_track_count_ >= config_.arm_ticks) {
      armed_ = true;
    }
    return false;
  }

  if (!in_track) {
    ++below_count_;
    if (below_count_ >= config_.window_ticks) {
      latched_ = true;
    }
  } else {
    below_count_ = 0;
  }
  return latched_;
}

}  // namespace bench
