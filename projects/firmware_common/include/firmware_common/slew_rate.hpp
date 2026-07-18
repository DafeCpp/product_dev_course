#pragma once

namespace firmware_common {

/** Limit the change from current to target using a time delta in seconds. */
inline float ApplySlewRate(float target, float current,
                           float max_change_per_sec, float dt_sec) noexcept {
  const float max_change = max_change_per_sec * dt_sec;
  const float diff = target - current;
  if (diff > max_change) {
    return current + max_change;
  }
  if (diff < -max_change) {
    return current - max_change;
  }
  return target;
}

}  // namespace firmware_common
