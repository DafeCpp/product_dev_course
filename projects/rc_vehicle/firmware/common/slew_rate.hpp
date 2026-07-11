#pragma once

#include <cstdint>

#include <firmware_common/slew_rate.hpp>

namespace rc_vehicle {

/**
 * Ограничение скорости изменения (slew-rate limiting).
 * Используется в control loop (ESP32-S3) для плавного изменения газа, руля
 * и весов стабилизации.
 */
inline float ApplySlewRate(float target, float current,
                           float max_change_per_sec, uint32_t dt_ms) noexcept {
  return firmware_common::ApplySlewRate(target, current, max_change_per_sec,
                                        dt_ms / 1000.0f);
}

}  // namespace rc_vehicle
