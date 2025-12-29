#include <stdio.h>

#include "config.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_server.hpp"
#include "uart_bridge.hpp"
#include "websocket_server.hpp"
#include "wifi_ap.hpp"

static const char* TAG = "main";

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "RC Vehicle ESP32-S3 firmware starting...");

  // Инициализация Wi-Fi AP
  ESP_LOGI(TAG, "Initializing Wi-Fi AP...");
  if (WiFiApInit() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize Wi-Fi AP");
    return;
  }

  // Инициализация UART моста к RP2040
  ESP_LOGI(TAG, "Initializing UART bridge...");
  if (UartBridgeInit() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize UART bridge");
    return;
  }

  // Инициализация HTTP сервера
  ESP_LOGI(TAG, "Initializing HTTP server...");
  if (HttpServerInit() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize HTTP server");
    return;
  }

  // Инициализация WebSocket сервера
  ESP_LOGI(TAG, "Initializing WebSocket server...");
  if (WebSocketServerInit() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize WebSocket server");
    return;
  }

  ESP_LOGI(TAG, "All systems initialized. Ready for connections.");

  // Основной цикл (задачи работают в отдельных потоках)
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    // Здесь можно добавить периодические задачи (heartbeat, статистика и т.д.)
  }
}
