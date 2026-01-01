#include "rc_input.hpp"

#include <math.h>

#include "config.hpp"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

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
        // Конвертация в значение [-1.0..1.0]
        float value = (float)(pulse_width - RC_IN_PULSE_NEUTRAL_US) /
                      (float)(RC_IN_PULSE_MAX_US - RC_IN_PULSE_NEUTRAL_US);
        if (value < -1.0f)
          value = -1.0f;
        if (value > 1.0f)
          value = 1.0f;
        last_throttle_value = value;
        last_throttle_pulse_time = now;
        last_rise_time_throttle = 0; // Сброс для следующего импульса
      }
    } else if (gpio == RC_IN_STEERING_PIN && last_rise_time_steering > 0) {
      uint32_t pulse_width = now - last_rise_time_steering;
      if (pulse_width >= RC_IN_PULSE_MIN_US &&
          pulse_width <= RC_IN_PULSE_MAX_US) {
        // Конвертация в значение [-1.0..1.0]
        float value = (float)(pulse_width - RC_IN_PULSE_NEUTRAL_US) /
                      (float)(RC_IN_PULSE_MAX_US - RC_IN_PULSE_NEUTRAL_US);
        if (value < -1.0f)
          value = -1.0f;
        if (value > 1.0f)
          value = 1.0f;
        last_steering_value = value;
        last_steering_pulse_time = now;
        last_rise_time_steering = 0; // Сброс для следующего импульса
      }
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

bool RcInputReadThrottle(float *throttle) {
  if (throttle == NULL) {
    return false;
  }

  uint32_t now = time_us_32();
  uint32_t time_since_last_pulse =
      (now - last_throttle_pulse_time) / 1000; // в миллисекундах

  if (time_since_last_pulse < RC_IN_TIMEOUT_MS) {
    *throttle = last_throttle_value;
    return true;
  }

  return false;
}

bool RcInputReadSteering(float *steering) {
  if (steering == NULL) {
    return false;
  }

  uint32_t now = time_us_32();
  uint32_t time_since_last_pulse =
      (now - last_steering_pulse_time) / 1000; // в миллисекундах

  if (time_since_last_pulse < RC_IN_TIMEOUT_MS) {
    *steering = last_steering_value;
    return true;
  }

  return false;
}

bool RcInputIsActive(void) {
  uint32_t now = time_us_32();
  uint32_t time_since_throttle = (now - last_throttle_pulse_time) / 1000;
  uint32_t time_since_steering = (now - last_steering_pulse_time) / 1000;

  return (time_since_throttle < RC_IN_TIMEOUT_MS &&
          time_since_steering < RC_IN_TIMEOUT_MS);
}
