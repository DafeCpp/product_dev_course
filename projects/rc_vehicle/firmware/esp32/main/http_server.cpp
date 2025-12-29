#include "http_server.hpp"

#include <string.h>

#include "config.hpp"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"

static const char* TAG = "http_server";
static httpd_handle_t server_handle = NULL;

// Обработчик для главной страницы
static esp_err_t root_get_handler(httpd_req_t* req) {
  // Пока возвращаем простой HTML (позже заменим на файл из SPIFFS)
  const char* html = R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>RC Vehicle Control</title>
    <style>
        body { font-family: Arial, sans-serif; text-align: center; padding: 20px; }
        .status { margin: 20px 0; padding: 10px; background: #f0f0f0; border-radius: 5px; }
        .connected { color: green; }
        .disconnected { color: red; }
    </style>
</head>
<body>
    <h1>RC Vehicle Control</h1>
    <div class="status">
        <p>WebSocket: <span id="ws-status" class="disconnected">Disconnected</span></p>
    </div>
    <p>WebSocket interface will be available at ws://192.168.4.1:81</p>
    <script>
        // WebSocket connection will be implemented in app.js
        console.log("RC Vehicle Control page loaded");
    </script>
</body>
</html>
)";

  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

esp_err_t HttpServerInit(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = HTTP_SERVER_PORT;
  config.max_uri_handlers = 8;

  ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);

  if (httpd_start(&server_handle, &config) == ESP_OK) {
    // Регистрация обработчика для главной страницы
    httpd_uri_t root_uri = {.uri = "/",
                            .method = HTTP_GET,
                            .handler = root_get_handler,
                            .user_ctx = NULL};
    httpd_register_uri_handler(server_handle, &root_uri);

    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
  }

  ESP_LOGE(TAG, "Failed to start HTTP server");
  return ESP_FAIL;
}
