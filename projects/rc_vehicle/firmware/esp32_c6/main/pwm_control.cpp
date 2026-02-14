#include "pwm_control.hpp"

#include <cstdint>

#include "config.hpp"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "rc_vehicle_common.hpp"

static const char* TAG = "pwm_control";

static constexpr ledc_mode_t kPwmSpeedMode = LEDC_LOW_SPEED_MODE;
static constexpr ledc_timer_t kPwmTimer = LEDC_TIMER_0;
static constexpr ledc_channel_t kThrottleChannel = LEDC_CHANNEL_0;
static constexpr ledc_channel_t kSteeringChannel = LEDC_CHANNEL_1;

static ledc_timer_bit_t s_duty_resolution = LEDC_TIMER_14_BIT;
static bool s_inited = false;

static uint32_t PeriodUs() {
  return static_cast<uint32_t>(1000000UL / PWM_FREQUENCY_HZ);
}

static uint32_t DutyMax() {
  return (1UL << static_cast<uint32_t>(s_duty_resolution)) - 1UL;
}

static uint32_t DutyFromPulseUs(uint16_t pulse_us) {
  const uint32_t period_us = PeriodUs();
  const uint32_t duty_max = DutyMax();
  uint32_t duty =
      static_cast<uint32_t>((static_cast<uint64_t>(pulse_us) * duty_max) /
                            static_cast<uint64_t>(period_us));
  if (duty > duty_max) duty = duty_max;
  return duty;
}

static int SetChannelPulseUs(ledc_channel_t channel, uint16_t pulse_us) {
  if (!s_inited) return -1;
  uint32_t duty = DutyFromPulseUs(pulse_us);
  esp_err_t e = ledc_set_duty(kPwmSpeedMode, channel, duty);
  if (e != ESP_OK) return -1;
  e = ledc_update_duty(kPwmSpeedMode, channel);
  return (e == ESP_OK) ? 0 : -1;
}

int PwmControlInit(void) {
  // Настройка таймера LEDC под 50 Hz servo PWM.
  ledc_timer_config_t timer_cfg = {};
  timer_cfg.speed_mode = kPwmSpeedMode;
  timer_cfg.timer_num = kPwmTimer;
  timer_cfg.duty_resolution = s_duty_resolution;
  timer_cfg.freq_hz = PWM_FREQUENCY_HZ;
  timer_cfg.clk_cfg = LEDC_AUTO_CLK;

  esp_err_t e = ledc_timer_config(&timer_cfg);
  if (e != ESP_OK) {
    // Fallback на 13-bit (иногда проще подобрать делитель).
    s_duty_resolution = LEDC_TIMER_13_BIT;
    timer_cfg.duty_resolution = s_duty_resolution;
    e = ledc_timer_config(&timer_cfg);
    if (e != ESP_OK) {
      ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(e));
      return -1;
    }
  }

  ledc_channel_config_t throttle_cfg = {};
  throttle_cfg.gpio_num = PWM_THROTTLE_PIN;
  throttle_cfg.speed_mode = kPwmSpeedMode;
  throttle_cfg.channel = kThrottleChannel;
  throttle_cfg.timer_sel = kPwmTimer;
  throttle_cfg.duty = 0;
  throttle_cfg.hpoint = 0;

  ledc_channel_config_t steering_cfg = throttle_cfg;
  steering_cfg.gpio_num = PWM_STEERING_PIN;
  steering_cfg.channel = kSteeringChannel;

  e = ledc_channel_config(&throttle_cfg);
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "LEDC throttle channel config failed: %s",
             esp_err_to_name(e));
    return -1;
  }
  e = ledc_channel_config(&steering_cfg);
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "LEDC steering channel config failed: %s",
             esp_err_to_name(e));
    return -1;
  }

  s_inited = true;
  PwmControlSetNeutral();

  ESP_LOGI(TAG, "PWM initialized: %d Hz, period=%lu us, duty_bits=%d",
           PWM_FREQUENCY_HZ, (unsigned long)PeriodUs(), (int)s_duty_resolution);
  return 0;
}

int PwmControlSetThrottle(float throttle) {
  const uint16_t pulse_us = rc_vehicle::PulseWidthUsFromNormalized(
      throttle, PWM_MIN_US, PWM_NEUTRAL_US, PWM_MAX_US);
  return SetChannelPulseUs(kThrottleChannel, pulse_us);
}

int PwmControlSetSteering(float steering) {
  const uint16_t pulse_us = rc_vehicle::PulseWidthUsFromNormalized(
      steering, PWM_MIN_US, PWM_NEUTRAL_US, PWM_MAX_US);
  return SetChannelPulseUs(kSteeringChannel, pulse_us);
}

void PwmControlSetNeutral(void) {
  (void)SetChannelPulseUs(kThrottleChannel, PWM_NEUTRAL_US);
  (void)SetChannelPulseUs(kSteeringChannel, PWM_NEUTRAL_US);
}
