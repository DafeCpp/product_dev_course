#include "rc_input.hpp"

#include "config.hpp"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "rc_vehicle_common.hpp"

static uint32_t last_throttle_pulse_time = 0;
static uint32_t last_steering_pulse_time = 0;
static float last_throttle_value = 0.0f;
static float last_steering_value = 0.0f;
static uint32_t last_rise_time_throttle = 0;
static uint32_t last_rise_time_steering = 0;

// Обработчик прерывания для измерения ширины импульса
static void gpio_callback(uint gpio, uint32_t events) {
  uint32_t now = time_us_32();

  if (events & GPIO_IRQ_EDGE_RISE) {
    // Запоминаем время фронта
    if (gpio == RC_IN_THROTTLE_PIN) {
      last_rise_time_throttle = now;
    } else if (gpio == RC_IN_STEERING_PIN) {
      last_rise_time_steering = now;
    }
  } else if (events & GPIO_IRQ_EDGE_FALL) {
    // Вычисляем ширину импульса
    if (gpio == RC_IN_THROTTLE_PIN && last_rise_time_throttle > 0) {
      uint32_t pulse_width = now - last_rise_time_throttle;
      if (pulse_width >= RC_IN_PULSE_MIN_US &&
          pulse_width <= RC_IN_PULSE_MAX_US) {
        last_throttle_value = rc_vehicle::NormalizedFromPulseWidthUs(
            pulse_width, RC_IN_PULSE_MIN_US, RC_IN_PULSE_NEUTRAL_US,
            RC_IN_PULSE_MAX_US);
        last_throttle_pulse_time = now;
      }
      // Сброс для следующего импульса — всегда (даже если ширина невалидна)
      last_rise_time_throttle = 0;
    } else if (gpio == RC_IN_STEERING_PIN && last_rise_time_steering > 0) {
      uint32_t pulse_width = now - last_rise_time_steering;
      if (pulse_width >= RC_IN_PULSE_MIN_US &&
          pulse_width <= RC_IN_PULSE_MAX_US) {
        last_steering_value = rc_vehicle::NormalizedFromPulseWidthUs(
            pulse_width, RC_IN_PULSE_MIN_US, RC_IN_PULSE_NEUTRAL_US,
            RC_IN_PULSE_MAX_US);
        last_steering_pulse_time = now;
      }
      // Сброс для следующего импульса — всегда (даже если ширина невалидна)
      last_rise_time_steering = 0;
    }
  }
}

int RcInputInit(void) {
  // Настройка GPIO как входы
  gpio_init(RC_IN_THROTTLE_PIN);
  gpio_init(RC_IN_STEERING_PIN);
  gpio_set_dir(RC_IN_THROTTLE_PIN, GPIO_IN);
  gpio_set_dir(RC_IN_STEERING_PIN, GPIO_IN);
  gpio_pull_up(RC_IN_THROTTLE_PIN);
  gpio_pull_up(RC_IN_STEERING_PIN);

  // Настройка прерываний для измерения ширины импульса
  gpio_set_irq_enabled_with_callback(RC_IN_THROTTLE_PIN,
                                     GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
                                     true, gpio_callback);
  gpio_set_irq_enabled(RC_IN_STEERING_PIN,
                       GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);

  return 0;
}

std::optional<float> RcInputReadThrottle(void) {
  uint32_t irq_state = save_and_disable_interrupts();
  uint32_t last_pulse_time = last_throttle_pulse_time;
  float last_value = last_throttle_value;
  restore_interrupts(irq_state);

  uint32_t now = time_us_32();
  uint32_t time_since_last_pulse =
      (now - last_pulse_time) / 1000;  // в миллисекундах

  if (time_since_last_pulse < RC_IN_TIMEOUT_MS) {
    return last_value;
  }
  return std::nullopt;
}

std::optional<float> RcInputReadSteering(void) {
  uint32_t irq_state = save_and_disable_interrupts();
  uint32_t last_pulse_time = last_steering_pulse_time;
  float last_value = last_steering_value;
  restore_interrupts(irq_state);

  uint32_t now = time_us_32();
  uint32_t time_since_last_pulse =
      (now - last_pulse_time) / 1000;  // в миллисекундах

  if (time_since_last_pulse < RC_IN_TIMEOUT_MS) {
    return last_value;
  }
  return std::nullopt;
}

bool RcInputIsActive(void) {
  uint32_t now = time_us_32();
  uint32_t time_since_throttle = (now - last_throttle_pulse_time) / 1000;
  uint32_t time_since_steering = (now - last_steering_pulse_time) / 1000;

  return (time_since_throttle < RC_IN_TIMEOUT_MS &&
          time_since_steering < RC_IN_TIMEOUT_MS);
}
