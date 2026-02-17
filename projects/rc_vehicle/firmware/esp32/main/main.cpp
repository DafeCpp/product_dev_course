#include <stdio.h>

#include <cstddef>

#include "cJSON.h"
#include "config.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_server.hpp"
#include "protocol.hpp"
#include "uart_bridge.hpp"
#include "websocket_server.hpp"
#include "wifi_ap.hpp"

static const char* TAG = "main";

static void ws_cmd_handler(float throttle, float steering) {
  (void)UartBridgeSendCommand(throttle, steering);
}

/** Интервал цикла UART/WebSocket (мс). */
static constexpr uint32_t UART_TASK_INTERVAL_MS = 20;
/** Стек задачи UART/телеметрии. */
static constexpr uint32_t UART_TASK_STACK = 4096;
/** Приоритет задачи UART. */
static constexpr UBaseType_t UART_TASK_PRIORITY = 5;

/**
 * Задача: периодический PING, приём PONG/телеметрии, отправка телеметрии в
 * WebSocket. Статус Pico/STM на странице опирается на mcu_pong_ok (ответ на
 * PING).
 */
static void uart_bridge_task(void* arg) {
  (void)arg;
  uint32_t last_ping_ms = 0;

  while (1) {
    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    // PING каждые PING_INTERVAL_MS
    if (now - last_ping_ms >= PING_INTERVAL_MS) {
      last_ping_ms = now;
      UartBridgeSendPing();
      ESP_LOGI(TAG, "PING отправлен");
    }

    // Приём PONG (неблокирующий)
    if (UartBridgeReceivePong() == ESP_OK) {
      ESP_LOGI(TAG, "PONG получен");
    }

    // Приём LOG-сообщений от MCU (все накопившиеся)
    {
      char log_buf[201];
      int log_len;
      while ((log_len = UartBridgeReceiveLog(log_buf, sizeof(log_buf) - 1)) >
             0) {
        log_buf[log_len] = '\0';
        // Убираем завершающий \n если есть (ESP_LOGI добавляет свой)
        if (log_len > 0 && log_buf[log_len - 1] == '\n') {
          log_buf[log_len - 1] = '\0';
        }
        ESP_LOGI("rp2040", "%s", log_buf);
      }
    }

    // Приём телеметрии (неблокирующий)

    if (auto telem = UartBridgeReceiveTelem()) {
      const TelemetryData& telem_data = *telem;
      // Сборка JSON для веб-страницы (формат как в app.js: type, link, imu,
      // mcu_pong_ok)
      cJSON* root = cJSON_CreateObject();
      if (root) {
        cJSON_AddStringToObject(root, "type", "telem");
        cJSON_AddBoolToObject(root, "mcu_pong_ok", UartBridgeIsMcuConnected());

        cJSON* link = cJSON_CreateObject();
        if (link) {
          cJSON_AddBoolToObject(link, "rc_ok", (telem_data.status & 0x01) != 0);
          cJSON_AddBoolToObject(link, "wifi_ok",
                                (telem_data.status & 0x02) != 0);
          cJSON_AddItemToObject(root, "link", link);
        }

        cJSON* imu = cJSON_CreateObject();
        if (imu) {
          cJSON_AddNumberToObject(imu, "ax", telem_data.ax);
          cJSON_AddNumberToObject(imu, "ay", telem_data.ay);
          cJSON_AddNumberToObject(imu, "az", telem_data.az);
          cJSON_AddNumberToObject(imu, "gx", telem_data.gx);
          cJSON_AddNumberToObject(imu, "gy", telem_data.gy);
          cJSON_AddNumberToObject(imu, "gz", telem_data.gz);
          cJSON_AddItemToObject(root, "imu", imu);
        }

        char* json_str = cJSON_PrintUnformatted(root);
        if (json_str) {
          WebSocketSendTelem(json_str);
          free(json_str);
        }
        cJSON_Delete(root);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(UART_TASK_INTERVAL_MS));
  }
}

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "RC Vehicle ESP32-C3 firmware starting...");

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

  // WebSocket команды управления → UART к MCU
  WebSocketSetCommandHandler(&ws_cmd_handler);

  // Инициализация HTTP сервера
  ESP_LOGI(TAG, "Initializing HTTP server...");
  if (HttpServerInit() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize HTTP server");
    return;
  }

  // Регистрация WebSocket URI на HTTP-сервере (один httpd на порту 80)
  ESP_LOGI(TAG, "Registering WebSocket handler...");
  if (WebSocketRegisterUri(HttpServerGetHandle()) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register WebSocket handler");
    return;
  }

  ESP_LOGI(TAG, "All systems initialized. Ready for connections.");

  char ap_ip[16];
  if (WiFiApGetIp(ap_ip, sizeof(ap_ip)) == ESP_OK) {
    ESP_LOGI(TAG, "----------------------------------------");
    ESP_LOGI(TAG, "  Подключитесь к Wi-Fi и откройте в браузере:");
    ESP_LOGI(TAG, "  http://%s", ap_ip);
    ESP_LOGI(TAG, "  WebSocket: ws://%s/ws", ap_ip);
    ESP_LOGI(TAG, "----------------------------------------");
  }

  // Задача: PING/PONG и пересылка телеметрии в WebSocket
  BaseType_t created = xTaskCreate(uart_bridge_task, "uart_ws", UART_TASK_STACK,
                                   NULL, UART_TASK_PRIORITY, NULL);
  if (created != pdPASS) {
    ESP_LOGE(TAG, "Failed to create uart_bridge task");
  }

  // Основной поток — idle
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
