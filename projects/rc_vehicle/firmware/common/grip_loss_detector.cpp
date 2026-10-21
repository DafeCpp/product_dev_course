#include "grip_loss_detector.hpp"

#include <algorithm>
#include <cmath>

namespace rc_vehicle {

void GripLossDetector::Reset() noexcept {
  reference_yaw_rate_rps_ = 0.0f;
  deviation_time_s_ = 0.0f;
  grip_lost_ = false;
}

void GripLossDetector::Update(float yaw_rate_rps, float vx_ms, float steering,
                              float dt_sec) noexcept {
  if (dt_sec <= 0.0f) return;
  if (vx_ms < kMinSpeedMs) {
    Reset();
    return;
  }
  const float steer = std::clamp(steering, -1.0f, 1.0f);
  reference_yaw_rate_rps_ = vx_ms * std::tan(steer * kMaxSteerRad) /
                            (kWheelbaseM + kUndersteerGradient * vx_ms * vx_ms);
  const float tolerance = std::max(
      kMaxYawDevRatio * std::abs(reference_yaw_rate_rps_), kMinYawDevRps);
  const bool deviates =
      std::abs(yaw_rate_rps - reference_yaw_rate_rps_) > tolerance;
  deviation_time_s_ = std::clamp(
      deviation_time_s_ + (deviates ? dt_sec : -dt_sec), 0.0f, kHoldS);
  if (deviation_time_s_ >= kHoldS) grip_lost_ = true;
  if (deviation_time_s_ <= 0.0f) grip_lost_ = false;
}

}  // namespace rc_vehicle
