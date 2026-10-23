#include "vesc_uart.hpp"

#include <cstdint>

#include "config.hpp"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "vesc_protocol.hpp"

static const char* TAG = "vesc_uart";

static constexpr uart_port_t kPort = VESC_UART_PORT;
static constexpr int kRxBufSize = 512;
// Ответ GET_VALUES ~80 байт ≈ 7 мс на 115200; запас на обработку в VESC.
static constexpr uint32_t kResponseTimeoutMs = 15;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static rc_vehicle::EscTelemetry s_last{};
static int64_t s_last_us = 0;  // 0 — ответов ещё не было
static bool s_started = false;

static void PollOnce(rc_vehicle::vesc::PacketParser& parser) {
  uint8_t req[8];
  const size_t req_len = rc_vehicle::vesc::BuildGetValuesRequest(req);
  if (req_len == 0) return;

  // Хвост прошлого ответа (пришёл после таймаута) не должен склеиться с новым.
  uart_flush_input(kPort);
  parser.Reset();
  uart_write_bytes(kPort, req, req_len);

  const int64_t deadline_us =
      esp_timer_get_time() + static_cast<int64_t>(kResponseTimeoutMs) * 1000;
  uint8_t buf[64];
  while (true) {
    const int64_t left_us = deadline_us - esp_timer_get_time();
    if (left_us <= 0) return;
    const TickType_t ticks =
        pdMS_TO_TICKS(static_cast<uint32_t>(left_us / 1000) + 1);
    const int n = uart_read_bytes(kPort, buf, sizeof(buf), ticks);
    if (n <= 0) continue;
    for (int i = 0; i < n; ++i) {
      const auto payload = parser.Feed(buf[i]);
      if (!payload) continue;
      const auto telem = rc_vehicle::vesc::ParseGetValues(*payload);
      if (!telem) continue;  // чужой пакет — ждём свой до дедлайна
      const int64_t now_us = esp_timer_get_time();
      taskENTER_CRITICAL(&s_lock);
      s_last = *telem;
      s_last_us = now_us;
      taskEXIT_CRITICAL(&s_lock);
      return;
    }
  }
}

static void VescPollTask(void* arg) {
  (void)arg;
  rc_vehicle::vesc::PacketParser parser;
  TickType_t last_wake = xTaskGetTickCount();
  uint32_t polls = 0;
  for (;;) {
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(VESC_POLL_INTERVAL_MS));
    PollOnce(parser);
    // Раз в ~10 с — счётчик битых кадров: помогает отличить помехи на линии
    // от неподключённого/неверно настроенного VESC (тогда ответов нет вовсе).
    if (++polls % (10000 / VESC_POLL_INTERVAL_MS) == 0 &&
        parser.ErrorCount() > 0) {
      ESP_LOGW(TAG, "bad frames so far: %lu",
               (unsigned long)parser.ErrorCount());
    }
  }
}

int VescUartInit(void) {
  if (s_started) return 0;

  uart_config_t cfg = {};
  cfg.baud_rate = VESC_UART_BAUD;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;

  esp_err_t e = uart_driver_install(kPort, kRxBufSize, 0, 0, nullptr, 0);
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(e));
    return -1;
  }
  e = uart_param_config(kPort, &cfg);
  if (e == ESP_OK) {
    e = uart_set_pin(kPort, VESC_UART_TX_PIN, VESC_UART_RX_PIN,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  }
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "UART config failed: %s", esp_err_to_name(e));
    uart_driver_delete(kPort);
    return -1;
  }

  // core 0, низкий приоритет: не конкурирует с control loop на core 1.
  if (xTaskCreatePinnedToCore(&VescPollTask, "vesc_poll", 3072, nullptr, 3,
                              nullptr, 0) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create poll task");
    uart_driver_delete(kPort);
    return -1;
  }

  s_started = true;
  ESP_LOGI(TAG, "VESC telemetry: UART%d %d baud, TX=%d RX=%d, every %d ms",
           (int)kPort, VESC_UART_BAUD, (int)VESC_UART_TX_PIN,
           (int)VESC_UART_RX_PIN, VESC_POLL_INTERVAL_MS);
  return 0;
}

std::optional<rc_vehicle::EscTelemetry> VescUartGetTelemetry(void) {
  if (!s_started) return std::nullopt;

  taskENTER_CRITICAL(&s_lock);
  const rc_vehicle::EscTelemetry last = s_last;
  const int64_t last_us = s_last_us;
  taskEXIT_CRITICAL(&s_lock);

  if (last_us == 0) return std::nullopt;
  const int64_t age_ms = (esp_timer_get_time() - last_us) / 1000;
  if (age_ms > VESC_TELEM_STALE_MS) return std::nullopt;

  rc_vehicle::EscTelemetry out = last;
  out.age_ms = static_cast<uint32_t>(age_ms);
  return out;
}
