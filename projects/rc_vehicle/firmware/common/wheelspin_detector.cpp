#include "wheelspin_detector.hpp"

#include <algorithm>

namespace rc_vehicle {

namespace {
constexpr float kG = 9.80665f;
}  // namespace

void WheelspinDetector::Reset(float wheel_speed_ms) noexcept {
  ground_speed_ms_ = wheel_speed_ms;
  slip_ratio_ = 0.0f;
  driving_ = false;
}

void WheelspinDetector::Update(float wheel_speed_ms, float wheel_target_ms,
                               float forward_accel_g, float dt_sec) noexcept {
  if (dt_sec <= 0.0f) return;
  driving_ = wheel_target_ms - wheel_speed_ms > kMinDriveGapMs;
  ground_speed_ms_ += forward_accel_g * kG * dt_sec +
                      (wheel_speed_ms - ground_speed_ms_) * dt_sec / kLeakTauS;
  if (wheel_speed_ms <= 0.0f) {
    slip_ratio_ = 0.0f;
    return;
  }
  slip_ratio_ = std::max(0.0f, (wheel_speed_ms - ground_speed_ms_) /
                                   std::max(wheel_speed_ms, kMinWheelSpeedMs));
}

}  // namespace rc_vehicle
