#include "pwm_control.hpp"

#include "config.hpp"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"
#include "rc_vehicle_common.hpp"

static uint pwm_throttle_slice = 0;
static uint pwm_steering_slice = 0;

int PwmControlInit(void) {
  // Инициализация GPIO для PWM
  gpio_set_function(PWM_THROTTLE_PIN, GPIO_FUNC_PWM);
  gpio_set_function(PWM_STEERING_PIN, GPIO_FUNC_PWM);

  // Получение срезов PWM
  pwm_throttle_slice = pwm_gpio_to_slice_num(PWM_THROTTLE_PIN);
  pwm_steering_slice = pwm_gpio_to_slice_num(PWM_STEERING_PIN);

  // Настройка частоты PWM (50 Hz = период 20 мс)
  // Частота системных часов обычно 125 MHz
  // Для 50 Hz с wrap=20000: div = 125000000 / (50 * 20000) = 125
  // Используем wrap=20000 для точности (1 тик = 1 мкс при 125 MHz / 125 = 1
  // MHz)
  float div = (float)clock_get_hz(clk_sys) / (PWM_FREQUENCY_HZ * 20000.0f);
  pwm_set_clkdiv(pwm_throttle_slice, div);
  pwm_set_clkdiv(pwm_steering_slice, div);

  // Установка периода (20000 тиков = 20 мс при 50 Hz, 1 тик = 1 мкс)
  pwm_set_wrap(pwm_throttle_slice, 20000);
  pwm_set_wrap(pwm_steering_slice, 20000);

  // Установка начальных значений (нейтраль)
  PwmControlSetNeutral();

  // Включение PWM
  pwm_set_enabled(pwm_throttle_slice, true);
  pwm_set_enabled(pwm_steering_slice, true);

  return 0;
}

int PwmControlSetThrottle(float throttle) {
  uint16_t pulse_us = rc_vehicle::PulseWidthUsFromNormalized(
      throttle, PWM_MIN_US, PWM_NEUTRAL_US, PWM_MAX_US);
  // Конвертация микросекунд в тики PWM (20000 тиков = 20 мс, 1 тик = 1 мкс)
  uint16_t level = pulse_us;
  pwm_set_chan_level(pwm_throttle_slice, pwm_gpio_to_channel(PWM_THROTTLE_PIN),
                     level);
  return 0;
}

int PwmControlSetSteering(float steering) {
  uint16_t pulse_us = rc_vehicle::PulseWidthUsFromNormalized(
      steering, PWM_MIN_US, PWM_NEUTRAL_US, PWM_MAX_US);
  // Конвертация микросекунд в тики PWM (20000 тиков = 20 мс, 1 тик = 1 мкс)
  uint16_t level = pulse_us;
  pwm_set_chan_level(pwm_steering_slice, pwm_gpio_to_channel(PWM_STEERING_PIN),
                     level);
  return 0;
}

void PwmControlSetNeutral(void) {
  uint16_t level = PWM_NEUTRAL_US;
  pwm_set_chan_level(pwm_throttle_slice, pwm_gpio_to_channel(PWM_THROTTLE_PIN),
                     level);
  pwm_set_chan_level(pwm_steering_slice, pwm_gpio_to_channel(PWM_STEERING_PIN),
                     level);
}
