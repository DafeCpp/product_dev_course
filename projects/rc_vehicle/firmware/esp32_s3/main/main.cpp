#include <stdio.h>
#include <string.h>

#include <firmware_common/esp32/dns_server.hpp>
#include <firmware_common/esp32/http_server.hpp>
#include <firmware_common/esp32/websocket_server.hpp>
#include <firmware_common/esp32/wifi_ap.hpp>

#include "cJSON.h"
#include "config.hpp"
#include "crash_logger.hpp"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "rc_http_routes.hpp"
#include "udp_telem_sender.hpp"
#include "vehicle_control.hpp"
#include "ws_command_handlers.hpp"
#include "ws_command_registry.hpp"

using namespace firmware_common::esp32;

static const char* TAG = "main";

// Global command registry
static rc_vehicle::WsCommandRegistry g_command_registry;

/**
 * Обработчик произвольных JSON-команд через WebSocket.
 * Использует registry pattern для диспетчеризации команд.
 */
static void ws_json_handler(const char* type, cJSON* json, httpd_req_t* req) {
  auto& vc = detail::GetVehicleControl();
  if (!g_command_registry.Handle(vc, type, json, req)) {
    ESP_LOGW(TAG, "Unknown WebSocket command type: %s", type);
  }
}

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "RC Vehicle ESP32-S3 firmware starting...");

  // Инициализация Wi-Fi AP (rc_vehicle: радио всегда включено, поведение не
  // меняется — сравните с runtime start/stop у головы, LOS-180).
  ESP_LOGI(TAG, "Initializing Wi-Fi AP...");
  if (WiFiApInit({.ssid_prefix = WIFI_AP_SSID_PREFIX,
                  .password = WIFI_AP_PASSWORD,
                  .channel = WIFI_AP_CHANNEL,
                  .max_connections = WIFI_AP_MAX_CONNECTIONS,
                  .radio_on_by_default = true}) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize Wi-Fi AP");
    return;
  }

  // Проверить причину перезагрузки и сохранить crash info в NVS при
  // необходимости. NVS инициализируется внутри WiFiApInit(), поэтому вызываем
  // сразу после него.
  CrashLoggerInit();

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
  if (HttpServerInit({.port = HTTP_SERVER_PORT}) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize HTTP server");
    return;
  }
  if (RcHttpRegisterRoutes(HttpServerGetHandle()) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register RC HTTP routes");
    return;
  }

  // Инициализация управления (PWM/RC/IMU/failsafe + телеметрия)
  ESP_LOGI(TAG, "Initializing vehicle control...");
  if (VehicleControlInit() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize vehicle control");
    return;
  }

  // Инициализация UDP-стриминга телеметрии
  ESP_LOGI(TAG, "Initializing UDP telemetry streamer...");
  if (UdpTelemInit() != ESP_OK) {
    ESP_LOGW(TAG, "UDP telemetry streamer init failed (non-fatal)");
  }

  // Регистрация обработчиков WebSocket команд
  ESP_LOGI(TAG, "Registering WebSocket command handlers...");
  // Команда управления throttle/steering — раньше отдельный колбэк
  // websocket_server, теперь обычная запись в registry (LOS-184).
  g_command_registry.Register("cmd", rc_vehicle::HandleControlCmd);
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
  g_command_registry.Register("toggle_kids_mode",
                              rc_vehicle::HandleToggleKidsMode);
  g_command_registry.Register("calibrate_steering_trim",
                              rc_vehicle::HandleCalibrateSteeringTrim);
  g_command_registry.Register("get_steering_trim_status",
                              rc_vehicle::HandleGetSteeringTrimStatus);
  g_command_registry.Register("calibrate_com_offset",
                              rc_vehicle::HandleCalibrateComOffset);
  g_command_registry.Register("get_com_offset_status",
                              rc_vehicle::HandleGetComOffsetStatus);
  g_command_registry.Register("start_test", rc_vehicle::HandleStartTest);
  g_command_registry.Register("stop_test", rc_vehicle::HandleStopTest);
  g_command_registry.Register("get_test_status",
                              rc_vehicle::HandleGetTestStatus);
  g_command_registry.Register("start_speed_calib",
                              rc_vehicle::HandleStartSpeedCalib);
  g_command_registry.Register("stop_speed_calib",
                              rc_vehicle::HandleStopSpeedCalib);
  g_command_registry.Register("get_speed_calib_status",
                              rc_vehicle::HandleGetSpeedCalibStatus);
  g_command_registry.Register("run_self_test", rc_vehicle::HandleRunSelfTest);
  g_command_registry.Register("udp_stream_start",
                              rc_vehicle::HandleUdpStreamStart);
  g_command_registry.Register("udp_stream_stop",
                              rc_vehicle::HandleUdpStreamStop);
  g_command_registry.Register("udp_stream_status",
                              rc_vehicle::HandleUdpStreamStatus);
  g_command_registry.Register("calibrate_mag", rc_vehicle::HandleCalibrateMag);
  g_command_registry.Register("get_mag_calib_status",
                              rc_vehicle::HandleGetMagCalibStatus);
  g_command_registry.Register("reset_heading_ref",
                              rc_vehicle::HandleResetHeadingRef);
  ESP_LOGI(TAG, "Registered %zu command handlers",
           g_command_registry.GetHandlerCount());

  // WebSocket JSON-команды (управление, калибровка и т.д. — все типы кадров)
  WebSocketSetJsonHandler(&ws_json_handler);

  // Регистрация WebSocket URI на HTTP-сервере (один httpd на порту 80)
  ESP_LOGI(TAG, "Registering WebSocket handler...");
  if (WebSocketRegisterUri(HttpServerGetHandle()) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register WebSocket handler");
    return;
  }
  if (rc_vehicle::RcWsTelemStart() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start WS telemetry channel");
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
