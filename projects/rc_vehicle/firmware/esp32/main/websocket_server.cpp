#include "websocket_server.hpp"

#include <string.h>

#include "cJSON.h"
#include "config.hpp"
#include "esp_http_server.h"
#include "esp_log.h"
#include "uart_bridge.hpp"

static const char* TAG = "websocket";
static httpd_handle_t ws_server_handle = NULL;

// Обработчик WebSocket подключения
static esp_err_t ws_handler(httpd_req_t* req) {
  if (req->method == HTTP_GET) {
    ESP_LOGI(TAG, "WebSocket connection request");
    return ESP_OK;
  }

  httpd_ws_frame_t ws_pkt;
  uint8_t* buf = (uint8_t*)calloc(1, WS_RX_BUFFER_SIZE);
  if (buf == NULL) {
    ESP_LOGE(TAG, "Failed to allocate buffer");
    return ESP_ERR_NO_MEM;
  }

  ws_pkt.payload = buf;
  ws_pkt.type = HTTPD_WS_TYPE_TEXT;

  while (1) {
    // Получение WebSocket фрейма
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, WS_RX_BUFFER_SIZE);
    if (ret != ESP_OK) {
      ESP_LOGI(TAG, "WebSocket connection closed");
      break;
    }

    if (ws_pkt.len == 0) {
      continue;
    }

    // Парсинг JSON команды
    cJSON* json = cJSON_Parse((char*)ws_pkt.payload);
    if (json == NULL) {
      ESP_LOGW(TAG, "Failed to parse JSON");
      continue;
    }

    cJSON* type = cJSON_GetObjectItem(json, "type");
    if (type && strcmp(type->valuestring, "cmd") == 0) {
      // Извлечение throttle и steering
      cJSON* throttle = cJSON_GetObjectItem(json, "throttle");
      cJSON* steer = cJSON_GetObjectItem(json, "steering");
      if (!throttle) throttle = cJSON_GetObjectItem(json, "thr");
      if (!steer) steer = cJSON_GetObjectItem(json, "steer");

      if (throttle && steer) {
        float thr = (float)throttle->valuedouble;
        float str = (float)steer->valuedouble;

        // Отправка команды на RP2040 через UART
        UartBridgeSendCommand(thr, str);
      }
    }

    cJSON_Delete(json);
  }

  free(buf);
  return ESP_OK;
}

// Обработчик для WebSocket endpoint
static const httpd_uri_t ws_uri = {.uri = "/ws",
                                   .method = HTTP_GET,
                                   .handler = ws_handler,
                                   .user_ctx = NULL,
                                   .is_websocket = true};

esp_err_t WebSocketServerInit(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = WEBSOCKET_SERVER_PORT;

  ESP_LOGI(TAG, "Starting WebSocket server on port %d", config.server_port);

  if (httpd_start(&ws_server_handle, &config) == ESP_OK) {
    httpd_register_uri_handler(ws_server_handle, &ws_uri);
    ESP_LOGI(TAG, "WebSocket server started");
    return ESP_OK;
  }

  ESP_LOGE(TAG, "Failed to start WebSocket server");
  return ESP_FAIL;
}

esp_err_t WebSocketSendTelem(const char* telem_json) {
  if (ws_server_handle == NULL || telem_json == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  size_t clients = httpd_get_client_count(ws_server_handle);
  if (clients == 0) {
    return ESP_OK;  // Нет клиентов, нечего отправлять
  }

  // Отправка всем подключенным клиентам
  // TODO: реализовать отправку всем клиентам через httpd_ws_send_frame
  // Пока заглушка
  return ESP_OK;
}

uint8_t WebSocketGetClientCount(void) {
  if (ws_server_handle == NULL) {
    return 0;
  }
  return httpd_get_client_count(ws_server_handle);
}
