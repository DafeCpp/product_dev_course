#include "uart_bridge.hpp"

#include "config.hpp"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "uart_bridge_base.hpp"

static const char *TAG = "uart_bridge";
static QueueHandle_t uart_queue = NULL;

class Esp32UartBridge : public UartBridgeBase {
 public:
  int Init() override {
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {},
    };
    esp_err_t e = uart_driver_install(UART_PORT_NUM, RX_BUF_SIZE * 2, 0, 20,
                                      &uart_queue, 0);
    if (e != ESP_OK) return -1;
    e = uart_param_config(UART_PORT_NUM, &uart_config);
    if (e != ESP_OK) return -1;
    e = uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (e != ESP_OK) return -1;
    rx_pos_ = 0;
    ESP_LOGI(TAG, "UART bridge initialized (baud: %d)", UART_BAUD_RATE);
    return 0;
  }

  int Write(const uint8_t *data, size_t len) override {
    if (data == nullptr) return -1;
    int n = uart_write_bytes(UART_PORT_NUM, data, len);
    return (n == static_cast<int>(len)) ? 0 : -1;
  }

  int ReadAvailable(uint8_t *buf, size_t max_len) override {
    if (buf == nullptr || max_len == 0) return 0;
    int n = uart_read_bytes(UART_PORT_NUM, buf, max_len, 0);
    return (n >= 0) ? n : -1;
  }
};

static Esp32UartBridge s_bridge;

esp_err_t UartBridgeInit(void) {
  return s_bridge.Init() == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t UartBridgeSendCommand(float throttle, float steering) {
  if (throttle > 1.0f) throttle = 1.0f;
  if (throttle < -1.0f) throttle = -1.0f;
  if (steering > 1.0f) steering = 1.0f;
  if (steering < -1.0f) steering = -1.0f;
  return s_bridge.SendCommand(throttle, steering) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t UartBridgeReceiveTelem(void *telem_data) {
  if (telem_data == nullptr) return ESP_ERR_INVALID_ARG;
  auto opt = s_bridge.ReceiveTelem();
  if (!opt) return ESP_ERR_NOT_FOUND;
  *static_cast<TelemetryData *>(telem_data) = *opt;
  return ESP_OK;
}
