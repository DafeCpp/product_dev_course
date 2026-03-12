#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "config.hpp"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "dns_server.hpp"
#include "http_server.hpp"
#include "vehicle_control.hpp"
#include "websocket_server.hpp"
#include "wifi_ap.hpp"
#include "ws_command_handlers.hpp"
#include "ws_command_registry.hpp"

static const char* TAG = "main";

// Global command registry
static rc_vehicle::WsCommandRegistry g_command_registry;

static void ws_cmd_handler(float throttle, float steering) {
  VehicleControlOnWifiCommand(throttle, steering);
}

/**
 * Обработчик произвольных JSON-команд через WebSocket.
 * Использует registry pattern для диспетчеризации команд.
 */
static void ws_json_handler(const char* type, cJSON* json, httpd_req_t* req) {
  if (!g_command_registry.Handle(type, json, req)) {
    ESP_LOGW(TAG, "Unknown WebSocket command type: %s", type);
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

  char ap_ip[16] = {};
  if (WiFiApGetIp(ap_ip, sizeof(ap_ip)) == ESP_OK) {
    const uint32_t ap_ip_raw = ipaddr_addr(ap_ip);
    if (ap_ip_raw == IPADDR_NONE) {
      ESP_LOGW(TAG, "Failed to parse AP IP for DNS server: %s", ap_ip);
    } else if (DnsServerStart(ap_ip_raw) != ESP_OK) {
      ESP_LOGW(TAG, "Failed to start DNS server");
    }
  } else {
    ESP_LOGW(TAG, "Failed to get AP IP for DNS server");
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

  // Регистрация обработчиков WebSocket команд
  ESP_LOGI(TAG, "Registering WebSocket command handlers...");
  g_command_registry.Register("calibrate_imu", rc_vehicle::HandleCalibrateImu);
  g_command_registry.Register("get_calib_status",
                              rc_vehicle::HandleGetCalibStatus);
  g_command_registry.Register("set_forward_direction",
                              rc_vehicle::HandleSetForwardDirection);
  g_command_registry.Register("get_stab_config",
                              rc_vehicle::HandleGetStabConfig);
  g_command_registry.Register("set_stab_config",
                              rc_vehicle::HandleSetStabConfig);
  g_command_registry.Register("get_log_info", rc_vehicle::HandleGetLogInfo);
  g_command_registry.Register("get_log_data", rc_vehicle::HandleGetLogData);
  g_command_registry.Register("clear_log", rc_vehicle::HandleClearLog);
  g_command_registry.Register("set_kids_preset",
                              rc_vehicle::HandleSetKidsPreset);
  g_command_registry.Register("get_kids_presets",
                              rc_vehicle::HandleGetKidsPresets);
  ESP_LOGI(TAG, "Registered %zu command handlers",
           g_command_registry.GetHandlerCount());

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
