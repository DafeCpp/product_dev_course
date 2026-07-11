#pragma once

#include <firmware_common/slew_rate.hpp>

namespace bench {

/**
 * Ограничение скорости изменения (slew-rate limiting).
 * Копия rc_vehicle::ApplySlewRate с dt в секундах: используется для
 * плавного изменения команды клапану (защита золотника от ступеней).
 */
inline float ApplySlewRate(float target, float current,
                           float max_change_per_sec, float dt_sec) noexcept {
  return firmware_common::ApplySlewRate(target, current, max_change_per_sec,
                                        dt_sec);
}

}  // namespace bench
