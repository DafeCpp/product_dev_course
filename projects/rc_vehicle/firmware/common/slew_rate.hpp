#pragma once

#include <cstdint>

namespace rc_vehicle {

/**
 * Ограничение скорости изменения (slew-rate limiting).
 * Используется в control loop (ESP32-S3) для плавного изменения газа, руля
 * и весов стабилизации.
 */
inline float ApplySlewRate(float target, float current,
                           float max_change_per_sec, uint32_t dt_ms) {
  float max_change = max_change_per_sec * (dt_ms / 1000.0f);
  float diff = target - current;
  if (diff > max_change)
    return current + max_change;
  if (diff < -max_change)
    return current - max_change;
  return target;
}

}  // namespace rc_vehicle
