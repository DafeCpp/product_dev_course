#include "uart_bridge.hpp"

#include "config.hpp"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "protocol.hpp"

static const char* TAG = "uart_bridge";
static QueueHandle_t uart_queue = NULL;

esp_err_t UartBridgeInit(void) {
  // Конфигурация UART
  uart_config_t uart_config = {
      .baud_rate = UART_BAUD_RATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE * 2, 0, 20,
                                      &uart_queue, 0));
  ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN,
                               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

  ESP_LOGI(TAG, "UART bridge initialized (baud: %d, TX: %d, RX: %d)",
           UART_BAUD_RATE, UART_TX_PIN, UART_RX_PIN);

  return ESP_OK;
}

esp_err_t UartBridgeSendCommand(float throttle, float steering) {
  // Ограничение значений
  if (throttle > 1.0f) throttle = 1.0f;
  if (throttle < -1.0f) throttle = -1.0f;
  if (steering > 1.0f) steering = 1.0f;
  if (steering < -1.0f) steering = -1.0f;

  // Формирование кадра COMMAND через протокол
  uint8_t frame[64];
  size_t frame_len =
      ProtocolBuildCommand(frame, sizeof(frame), throttle, steering);

  if (frame_len == 0) {
    ESP_LOGE(TAG, "Failed to build command frame");
    return ESP_FAIL;
  }

  // Отправка через UART
  int len = uart_write_bytes(UART_PORT_NUM, frame, frame_len);
  if (len != frame_len) {
    ESP_LOGE(TAG, "Failed to send command (sent %d/%zu bytes)", len, frame_len);
    return ESP_FAIL;
  }

  return ESP_OK;
}

esp_err_t UartBridgeReceiveTelem(void* telem_data) {
  if (uart_queue == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  uart_event_t event;
  if (xQueueReceive(uart_queue, &event, 0) == pdTRUE) {
    if (event.type == UART_DATA) {
      uint8_t data[UART_BUF_SIZE];
      int len = uart_read_bytes(UART_PORT_NUM, data, event.size, 0);
      if (len > 0) {
        // Парсинг телеметрии через протокол
        // TODO: реализовать парсинг кадров TELEM
        ESP_LOGI(TAG, "Received %d bytes from RP2040", len);
      }
    }
  }

  return ESP_ERR_NOT_FOUND;
}
