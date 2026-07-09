#pragma once

namespace bench {

/**
 * Ограничение скорости изменения (slew-rate limiting).
 * Копия rc_vehicle::ApplySlewRate с dt в секундах: используется для
 * плавного изменения команды клапану (защита золотника от ступеней).
 */
inline float ApplySlewRate(float target, float current,
                           float max_change_per_sec, float dt_sec) {
  float max_change = max_change_per_sec * dt_sec;
  float diff = target - current;
  if (diff > max_change) return current + max_change;
  if (diff < -max_change) return current - max_change;
  return target;
}

}  // namespace bench
