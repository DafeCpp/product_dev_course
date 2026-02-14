#include "rc_input.hpp"

#include <cstdint>

#include "config.hpp"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "rc_vehicle_common.hpp"

static const char* TAG = "rc_input";

static portMUX_TYPE s_rc_mux = portMUX_INITIALIZER_UNLOCKED;

static uint32_t s_last_throttle_pulse_time_us = 0;
static uint32_t s_last_steering_pulse_time_us = 0;
static uint32_t s_last_throttle_pulse_width_us = 0;
static uint32_t s_last_steering_pulse_width_us = 0;
static uint32_t s_last_rise_throttle_us = 0;
static uint32_t s_last_rise_steering_us = 0;

static void gpio_isr_handler(void* arg) {
  const uint32_t gpio_num = (uint32_t)arg;
  const int level = gpio_get_level((gpio_num_t)gpio_num);
  const uint32_t now_us = (uint32_t)esp_timer_get_time();

  portENTER_CRITICAL_ISR(&s_rc_mux);
  if (level) {
    // RISE
    if (gpio_num == (uint32_t)RC_IN_THROTTLE_PIN) {
      s_last_rise_throttle_us = now_us;
    } else if (gpio_num == (uint32_t)RC_IN_STEERING_PIN) {
      s_last_rise_steering_us = now_us;
    }
  } else {
    // FALL
    if (gpio_num == (uint32_t)RC_IN_THROTTLE_PIN &&
        s_last_rise_throttle_us != 0) {
      const uint32_t width_us = now_us - s_last_rise_throttle_us;
      if (width_us >= RC_IN_PULSE_MIN_US && width_us <= RC_IN_PULSE_MAX_US) {
        s_last_throttle_pulse_width_us = width_us;
        s_last_throttle_pulse_time_us = now_us;
      }
      s_last_rise_throttle_us = 0;
    } else if (gpio_num == (uint32_t)RC_IN_STEERING_PIN &&
               s_last_rise_steering_us != 0) {
      const uint32_t width_us = now_us - s_last_rise_steering_us;
      if (width_us >= RC_IN_PULSE_MIN_US && width_us <= RC_IN_PULSE_MAX_US) {
        s_last_steering_pulse_width_us = width_us;
        s_last_steering_pulse_time_us = now_us;
      }
      s_last_rise_steering_us = 0;
    }
  }
  portEXIT_CRITICAL_ISR(&s_rc_mux);
}

static int SetupRcGpio(gpio_num_t pin) {
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_ANYEDGE;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = (1ULL << (uint32_t)pin);
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_ENABLE;  // безопасно, если вход "висит"

  esp_err_t e = gpio_config(&io_conf);
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "gpio_config failed for pin %d: %s", (int)pin,
             esp_err_to_name(e));
    return -1;
  }
  return 0;
}

int RcInputInit(void) {
  if (SetupRcGpio(RC_IN_THROTTLE_PIN) != 0) return -1;
  if (SetupRcGpio(RC_IN_STEERING_PIN) != 0) return -1;

  esp_err_t e = gpio_install_isr_service(0);
  if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(e));
    return -1;
  }

  e = gpio_isr_handler_add(RC_IN_THROTTLE_PIN, gpio_isr_handler,
                           (void*)RC_IN_THROTTLE_PIN);
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "gpio_isr_handler_add throttle failed: %s", esp_err_to_name(e));
    return -1;
  }
  e = gpio_isr_handler_add(RC_IN_STEERING_PIN, gpio_isr_handler,
                           (void*)RC_IN_STEERING_PIN);
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "gpio_isr_handler_add steering failed: %s", esp_err_to_name(e));
    return -1;
  }

  ESP_LOGI(TAG, "RC input initialized (pins: thr=%d, steer=%d)",
           (int)RC_IN_THROTTLE_PIN, (int)RC_IN_STEERING_PIN);
  return 0;
}

static std::optional<float> ReadChannel(uint32_t last_time_us,
                                       uint32_t last_width_us) {
  if (last_time_us == 0) return std::nullopt;

  const uint32_t now_us = (uint32_t)esp_timer_get_time();
  const uint32_t dt_ms = (now_us - last_time_us) / 1000;
  if (dt_ms >= RC_IN_TIMEOUT_MS) return std::nullopt;

  return rc_vehicle::NormalizedFromPulseWidthUs(
      last_width_us, RC_IN_PULSE_MIN_US, RC_IN_PULSE_NEUTRAL_US,
      RC_IN_PULSE_MAX_US);
}

std::optional<float> RcInputReadThrottle(void) {
  uint32_t t_us = 0;
  uint32_t w_us = 0;
  portENTER_CRITICAL(&s_rc_mux);
  t_us = s_last_throttle_pulse_time_us;
  w_us = s_last_throttle_pulse_width_us;
  portEXIT_CRITICAL(&s_rc_mux);
  return ReadChannel(t_us, w_us);
}

std::optional<float> RcInputReadSteering(void) {
  uint32_t t_us = 0;
  uint32_t w_us = 0;
  portENTER_CRITICAL(&s_rc_mux);
  t_us = s_last_steering_pulse_time_us;
  w_us = s_last_steering_pulse_width_us;
  portEXIT_CRITICAL(&s_rc_mux);
  return ReadChannel(t_us, w_us);
}

bool RcInputIsActive(void) {
  uint32_t thr_us = 0;
  uint32_t str_us = 0;
  portENTER_CRITICAL(&s_rc_mux);
  thr_us = s_last_throttle_pulse_time_us;
  str_us = s_last_steering_pulse_time_us;
  portEXIT_CRITICAL(&s_rc_mux);

  const uint32_t now_us = (uint32_t)esp_timer_get_time();
  const uint32_t dt_thr = (now_us - thr_us) / 1000;
  const uint32_t dt_str = (now_us - str_us) / 1000;

  return (thr_us != 0 && str_us != 0 && dt_thr < RC_IN_TIMEOUT_MS &&
          dt_str < RC_IN_TIMEOUT_MS);
}

