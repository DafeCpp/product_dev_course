#pragma once

#include <cstdint>

/**
 * Ограничение скорости изменения (slew-rate limiting).
 * Используется в main loop RP2040/STM32 для плавного изменения газа и руля.
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
