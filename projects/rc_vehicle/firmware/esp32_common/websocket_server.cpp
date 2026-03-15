#include "websocket_server.hpp"

#include <string.h>

#include "cJSON.h"
#include "config.hpp"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char* TAG = "websocket";
static httpd_handle_t ws_server_handle = NULL;

/** Размер одного буфера телеметрии (JSON). */
static constexpr size_t TELEM_BUF_SIZE = 1024;
/** Очередь длины 1: индекс буфера (0 или 1), готового к отправке. */
static QueueHandle_t s_telem_queue = NULL;
static char s_telem_buf[2][TELEM_BUF_SIZE];
static uint8_t s_telem_write_idx = 0;

static void telem_sender_task(void* arg) {
  (void)arg;
  for (;;) {
    uint8_t idx;
    if (xQueueReceive(s_telem_queue, &idx, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    WebSocketSendTelem(s_telem_buf[idx]);
  }
}
static WebSocketCommandHandler s_cmd_handler = nullptr;
static WebSocketJsonHandler s_json_handler = nullptr;

void WebSocketSetCommandHandler(WebSocketCommandHandler handler) {
  s_cmd_handler = handler;
}

void WebSocketSetJsonHandler(WebSocketJsonHandler handler) {
  s_json_handler = handler;
}

// Обработчик WebSocket: вызывается один раз на каждый фрейм (как в
// ws_echo_server). Цикл while(1) ломал порядок: фреймворк не успевал читать
// opcode следующего фрейма → "not properly masked".
static esp_err_t ws_handler(httpd_req_t* req) {
  if (req->method == HTTP_GET) {
    ESP_LOGI(TAG, "WebSocket connection request");
    return ESP_OK;
  }

  // Локальный буфер: не static, чтобы избежать гонки при нескольких
  // одновременных WebSocket-соединениях.
  uint8_t buf[WS_RX_BUFFER_SIZE];
  httpd_ws_frame_t ws_pkt = {};
  ws_pkt.payload = buf;
  ws_pkt.len = 0;

  esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, WS_RX_BUFFER_SIZE);
  if (ret != ESP_OK) {
    return ret;
  }

  if (ws_pkt.len == 0 || ws_pkt.type != HTTPD_WS_TYPE_TEXT) {
    return ESP_OK;
  }

  size_t safe_len = ws_pkt.len;
  if (safe_len >= WS_RX_BUFFER_SIZE) {
    safe_len = WS_RX_BUFFER_SIZE - 1;
  }
  buf[safe_len] = '\0';

  cJSON* json = cJSON_Parse(reinterpret_cast<char*>(ws_pkt.payload));
  if (json == NULL) {
    ESP_LOGW(TAG, "Failed to parse JSON");
    return ESP_OK;  // не рвём соединение из‑за битого кадра
  }

  cJSON* type = cJSON_GetObjectItem(json, "type");
  if (type && cJSON_IsString(type)) {
    if (strcmp(type->valuestring, "cmd") == 0) {
      cJSON* throttle = cJSON_GetObjectItem(json, "throttle");
      cJSON* steer = cJSON_GetObjectItem(json, "steering");
      if (!throttle) throttle = cJSON_GetObjectItem(json, "thr");
      if (!steer) steer = cJSON_GetObjectItem(json, "steer");

      if (throttle && steer && s_cmd_handler) {
        float thr = static_cast<float>(throttle->valuedouble);
        float str = static_cast<float>(steer->valuedouble);
        s_cmd_handler(thr, str);
      }
    } else if (s_json_handler) {
      s_json_handler(type->valuestring, json, req);
    }
  }

  cJSON_Delete(json);
  return ESP_OK;
}

// Обработчик для WebSocket endpoint
static const httpd_uri_t ws_uri = {.uri = "/ws",
                                   .method = HTTP_GET,
                                   .handler = ws_handler,
                                   .user_ctx = NULL,
                                   .is_websocket = true,
                                   .handle_ws_control_frames = false,
                                   .supported_subprotocol = NULL};

esp_err_t WebSocketRegisterUri(httpd_handle_t server) {
  if (server == NULL) {
    ESP_LOGE(TAG, "Cannot register WS URI: server handle is NULL");
    return ESP_ERR_INVALID_ARG;
  }

  ws_server_handle = server;

  if (s_telem_queue == NULL) {
    s_telem_queue = xQueueCreate(1, sizeof(uint8_t));
    if (s_telem_queue != NULL) {
      const UBaseType_t prio = 5;
      if (xTaskCreate(telem_sender_task, "ws_telem", 3072, NULL, prio, NULL) !=
          pdPASS) {
        vQueueDelete(s_telem_queue);
        s_telem_queue = NULL;
      }
    }
  }

  esp_err_t ret = httpd_register_uri_handler(ws_server_handle, &ws_uri);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "WebSocket URI /ws registered");
  } else {
    ESP_LOGE(TAG, "Failed to register WebSocket URI: %s", esp_err_to_name(ret));
  }
  return ret;
}

void WebSocketEnqueueTelem(const char* telem_json) {
  if (telem_json == NULL || s_telem_queue == NULL) {
    return;
  }
  size_t len = strlen(telem_json);
  if (len >= TELEM_BUF_SIZE) {
    len = TELEM_BUF_SIZE - 1;
  }
  memcpy(s_telem_buf[s_telem_write_idx], telem_json, len);
  s_telem_buf[s_telem_write_idx][len] = '\0';
  const uint8_t idx = s_telem_write_idx;
  s_telem_write_idx = 1 - s_telem_write_idx;
  xQueueOverwrite(s_telem_queue, &idx);
}

esp_err_t WebSocketSendTelem(const char* telem_json) {
  if (ws_server_handle == NULL || telem_json == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  int client_fds[WEBSOCKET_MAX_CLIENTS];
  size_t client_count = WEBSOCKET_MAX_CLIENTS;
  if (httpd_get_client_list(ws_server_handle, &client_count, client_fds) !=
          ESP_OK ||
      client_count == 0) {
    return ESP_OK;  // Нет клиентов, нечего отправлять
  }

  size_t len = strlen(telem_json);
  httpd_ws_frame_t ws_pkt = {};
  ws_pkt.final = true;
  ws_pkt.fragmented = false;
  ws_pkt.type = HTTPD_WS_TYPE_TEXT;
  // ESP-IDF API принимает uint8_t*, но при отправке данные не модифицирует.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  ws_pkt.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(telem_json));
  ws_pkt.len = len;

  for (size_t i = 0; i < client_count; i++) {
    int fd = client_fds[i];
    if (httpd_ws_get_fd_info(ws_server_handle, fd) !=
        HTTPD_WS_CLIENT_WEBSOCKET) {
      continue;
    }
    if (httpd_ws_send_data(ws_server_handle, fd, &ws_pkt) != ESP_OK) {
      ESP_LOGW(TAG, "Failed to send telem to fd %d", fd);
    }
  }
  return ESP_OK;
}

uint8_t WebSocketGetClientCount(void) {
  if (ws_server_handle == NULL) {
    return 0;
  }
  int client_fds[WEBSOCKET_MAX_CLIENTS];
  size_t client_count = WEBSOCKET_MAX_CLIENTS;
  if (httpd_get_client_list(ws_server_handle, &client_count, client_fds) !=
      ESP_OK) {
    return 0;
  }
  return (uint8_t)client_count;
}
