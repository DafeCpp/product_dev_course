#include "rc_input.hpp"

#include <cstdint>

#include "config.hpp"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "rc_pulse_decoder.hpp"

static const char* TAG = "rc_input";

static portMUX_TYPE s_rc_mux = portMUX_INITIALIZER_UNLOCKED;

static constexpr rc_vehicle::RcPulseConfig kRcPulseConfig{
    .min_us = RC_IN_PULSE_MIN_US,
    .neutral_us = RC_IN_PULSE_NEUTRAL_US,
    .max_us = RC_IN_PULSE_MAX_US,
    .tolerance_us = RC_IN_PULSE_TOLERANCE_US,
    .timeout_us = RC_IN_TIMEOUT_MS * 1000u,
};

static rc_vehicle::RcPulseDecoder s_throttle(kRcPulseConfig);
static rc_vehicle::RcPulseDecoder s_steering(kRcPulseConfig);

static void gpio_isr_handler(void* arg) {
  const uint32_t gpio_num = (uint32_t)arg;
  const bool level = gpio_get_level((gpio_num_t)gpio_num) != 0;
  const uint64_t now_us = (uint64_t)esp_timer_get_time();

  portENTER_CRITICAL_ISR(&s_rc_mux);
  if (gpio_num == (uint32_t)RC_IN_THROTTLE_PIN) {
    s_throttle.OnEdge(level, now_us);
  } else if (gpio_num == (uint32_t)RC_IN_STEERING_PIN) {
    s_steering.OnEdge(level, now_us);
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

// Копия декодера (~64 байта) под спинлоком; сам разбор — вне критической
// секции, чтобы не удерживать ISR дольше необходимого.
static rc_vehicle::RcPulseDecoder SnapshotDecoder(
    const rc_vehicle::RcPulseDecoder& src) {
  portENTER_CRITICAL(&s_rc_mux);
  const rc_vehicle::RcPulseDecoder copy = src;
  portEXIT_CRITICAL(&s_rc_mux);
  return copy;
}

std::optional<float> RcInputReadThrottle(void) {
  return SnapshotDecoder(s_throttle).Read((uint64_t)esp_timer_get_time());
}

std::optional<float> RcInputReadSteering(void) {
  return SnapshotDecoder(s_steering).Read((uint64_t)esp_timer_get_time());
}

bool RcInputIsActive(void) {
  const uint64_t now_us = (uint64_t)esp_timer_get_time();
  return SnapshotDecoder(s_throttle).IsActive(now_us) &&
         SnapshotDecoder(s_steering).IsActive(now_us);
}
