#pragma once

#include <cstdint>

#include "esp_err.h"
#include "esp_http_server.h"

namespace firmware_common::esp32 {

/** Конфигурация каркаса HTTP-сервера (httpd + captive-portal + /api/wifi/*). */
struct HttpServerConfig {
  uint16_t port = 80;
  uint16_t max_uri_handlers = 18;
  uint32_t stack_size = 8192;
  uint8_t max_open_sockets = 5;
  uint8_t recv_wait_timeout_s = 5;
  uint8_t send_wait_timeout_s = 2;
  /**
   * Редиректы /generate_204, /gen_204, /hotspot-detect.html, /ncsi.txt,
   * /connecttest.txt, /redirect на "/" (captive-portal probes iOS/Android/
   * Windows/macOS).
   */
  bool enable_captive_portal = true;
  /** /api/wifi/status, /api/wifi/scan, /api/wifi/sta/connect|disconnect. */
  bool enable_wifi_api = true;
};

/**
 * Запустить httpd-сервер: каркас + (опционально) captive-portal-пробы и
 * /api/wifi/*. Специфичные маршруты потребитель регистрирует сам через
 * httpd_register_uri_handler(HttpServerGetHandle(), ...) после вызова.
 * @return ESP_OK при успехе, иначе код ошибки
 */
esp_err_t HttpServerInit(const HttpServerConfig& cfg = {});

/**
 * Получить handle запущенного HTTP-сервера (для регистрации доп. URI, напр.
 * WebSocket или маршрутов потребителя).
 * @return httpd_handle_t или NULL если сервер не запущен
 */
httpd_handle_t HttpServerGetHandle(void);

}  // namespace firmware_common::esp32
