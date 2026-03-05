#include "kids_mode_processor.hpp"

#include <algorithm>
#include <cmath>

#include "slew_rate.hpp"

namespace rc_vehicle {

void KidsModeProcessor::Init(const StabilizationConfig& cfg,
                             const VehicleEkf& ekf, const ImuHandler* imu) {
  cfg_ = &cfg;
  ekf_ = &ekf;
  imu_ = imu;
  Reset();
}

void KidsModeProcessor::Process(float& throttle, float& steering,
                                uint32_t dt_ms) noexcept {
  if (!cfg_ || !IsActive()) {
    return;  // Kids Mode не активен
  }

  const auto& km = cfg_->kids_mode;

  // ─────────────────────────────────────────────────────────────────────────
  // 1. Применить ограничения throttle/steering
  // ─────────────────────────────────────────────────────────────────────────

  // Ограничение газа: forward и reverse отдельно
  if (throttle > 0.0f) {
    throttle = std::min(throttle, km.throttle_limit);
  } else {
    throttle = std::max(throttle, -km.reverse_limit);
  }

  // Ограничение руля
  steering = std::clamp(steering, -km.steering_limit, km.steering_limit);

  // ─────────────────────────────────────────────────────────────────────────
  // 2. Применить усиленный slew rate (плавность)
  // ─────────────────────────────────────────────────────────────────────────

  if (dt_ms > 0) {
    smoothed_throttle_ =
        ApplySlewRate(throttle, smoothed_throttle_, km.slew_throttle, dt_ms);
    smoothed_steering_ =
        ApplySlewRate(steering, smoothed_steering_, km.slew_steering, dt_ms);

    throttle = smoothed_throttle_;
    steering = smoothed_steering_;
  }

  // ─────────────────────────────────────────────────────────────────────────
  // 3. Anti-spin защита (снижение газа при заносе)
  // ─────────────────────────────────────────────────────────────────────────

  anti_spin_active_ = false;

  if (km.anti_spin_enabled && ekf_ && imu_ && imu_->IsEnabled()) {
    const float slip_deg = std::abs(ekf_->GetSlipAngleDeg());

    if (slip_deg > km.anti_spin_threshold_deg) {
      anti_spin_active_ = true;
      // Снизить газ на anti_spin_reduction процентов
      throttle *= (1.0f - km.anti_spin_reduction);
    }
  }
}

bool KidsModeProcessor::IsActive() const noexcept {
  return cfg_ && cfg_->mode == DriveMode::Kids;
}

void KidsModeProcessor::Reset() noexcept {
  smoothed_throttle_ = 0.0f;
  smoothed_steering_ = 0.0f;
  anti_spin_active_ = false;
}

}  // namespace rc_vehicle