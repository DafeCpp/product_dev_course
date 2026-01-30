#pragma once

#include <cstdint>

/**
 * Общие утилиты RC Vehicle: конвертация PWM/RC (нормализованное значение
 * [-1..1] ↔ ширина импульса в микросекундах). Используются в pwm_control и
 * rc_input на RP2040/STM32.
 */

namespace rc_vehicle {

/** Ограничение value в диапазон [-1, 1]. */
inline float ClampNormalized(float value) {
  if (value < -1.0f)
    return -1.0f;
  if (value > 1.0f)
    return 1.0f;
  return value;
}

/**
 * Конвертация нормализованного значения [-1..1] в ширину импульса (мкс).
 * value = -1 → min_us, value = 0 → neutral_us, value = 1 → max_us.
 */
inline uint16_t PulseWidthUsFromNormalized(float value, uint16_t min_us,
                                           uint16_t neutral_us,
                                           uint16_t max_us) {
  value = ClampNormalized(value);
  float pulse_us;
  if (value >= 0.0f) {
    pulse_us = neutral_us + value * (max_us - neutral_us);
  } else {
    pulse_us = neutral_us + value * (neutral_us - min_us);
  }
  if (pulse_us < min_us)
    pulse_us = static_cast<float>(min_us);
  if (pulse_us > max_us)
    pulse_us = static_cast<float>(max_us);
  return static_cast<uint16_t>(pulse_us);
}

/**
 * Конвертация ширины импульса RC (мкс) в нормализованное значение [-1..1].
 * neutral_us → 0, min_us → -1, max_us → 1.
 */
inline float NormalizedFromPulseWidthUs(uint32_t pulse_us, uint16_t min_us,
                                        uint16_t neutral_us,
                                        uint16_t max_us) {
  float value =
      static_cast<float>(static_cast<int32_t>(pulse_us) - neutral_us) /
      static_cast<float>(pulse_us >= neutral_us ? (max_us - neutral_us)
                                                : (neutral_us - min_us));
  return ClampNormalized(value);
}

}  // namespace rc_vehicle
