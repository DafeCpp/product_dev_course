#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "config.hpp"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_server.hpp"
#include "vehicle_control.hpp"
#include "websocket_server.hpp"
#include "wifi_ap.hpp"

static const char* TAG = "main";

static void ws_cmd_handler(float throttle, float steering) {
  VehicleControlOnWifiCommand(throttle, steering);
}

/** Отправить JSON-ответ обратно в тот же WebSocket-фрейм. */
static void ws_send_reply(httpd_req_t* req, cJSON* reply) {
  char* str = cJSON_PrintUnformatted(reply);
  if (str) {
    httpd_ws_frame_t pkt = {};
    pkt.final = true;
    pkt.fragmented = false;
    pkt.type = HTTPD_WS_TYPE_TEXT;
    pkt.payload = reinterpret_cast<uint8_t*>(str);
    pkt.len = strlen(str);
    httpd_ws_send_frame(req, &pkt);
    free(str);
  }
}

/**
 * Обработчик произвольных JSON-команд через WebSocket.
 *
 * Протокол:
 *   → {"type":"calibrate_imu","mode":"gyro"}   — запуск калибровки гироскопа
 *   → {"type":"calibrate_imu","mode":"full"}    — полная калибровка
 *   ← {"type":"calibrate_imu_ack","status":"collecting"}
 *
 *   → {"type":"get_calib_status"}
 *   ← {"type":"calib_status","status":"done","valid":true}
 */
static void ws_json_handler(const char* type, cJSON* json, httpd_req_t* req) {
  if (strcmp(type, "calibrate_imu") == 0) {
    cJSON* mode = cJSON_GetObjectItem(json, "mode");
    bool full = (mode && cJSON_IsString(mode) && strcmp(mode->valuestring, "full") == 0);
    VehicleControlStartCalibration(full);
    ESP_LOGI(TAG, "WS: calibrate_imu (mode=%s)", full ? "full" : "gyro");

    cJSON* reply = cJSON_CreateObject();
    if (reply) {
      cJSON_AddStringToObject(reply, "type", "calibrate_imu_ack");
      cJSON_AddStringToObject(reply, "status", "collecting");
      cJSON_AddStringToObject(reply, "mode", full ? "full" : "gyro");
      ws_send_reply(req, reply);
      cJSON_Delete(reply);
    }
  } else if (strcmp(type, "get_calib_status") == 0) {
    cJSON* reply = cJSON_CreateObject();
    if (reply) {
      cJSON_AddStringToObject(reply, "type", "calib_status");
      cJSON_AddStringToObject(reply, "status", VehicleControlGetCalibStatus());
      ws_send_reply(req, reply);
      cJSON_Delete(reply);
    }
  }
}

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "RC Vehicle ESP32-S3 firmware starting...");

  // Инициализация Wi-Fi AP
  ESP_LOGI(TAG, "Initializing Wi-Fi AP...");
  if (WiFiApInit() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize Wi-Fi AP");
    return;
  }

  // Инициализация HTTP сервера
  ESP_LOGI(TAG, "Initializing HTTP server...");
  if (HttpServerInit() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize HTTP server");
    return;
  }

  // Инициализация управления (PWM/RC/IMU/failsafe + телеметрия)
  ESP_LOGI(TAG, "Initializing vehicle control...");
  if (VehicleControlInit() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize vehicle control");
    return;
  }

  // WebSocket команды управления → local control loop
  WebSocketSetCommandHandler(&ws_cmd_handler);
  // WebSocket JSON-команды (калибровка и т.д.)
  WebSocketSetJsonHandler(&ws_json_handler);

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

  // Основной поток — idle
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
