#include <string.h>

#include <atomic>
#include <firmware_common/esp32/websocket_server.hpp>
#include <firmware_common/ws_client_failure_tracker.hpp>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"

namespace firmware_common::esp32 {

static const char* TAG = "websocket";
static httpd_handle_t ws_server_handle = NULL;

/** Макс. число HTTP-соединений httpd (для httpd_get_client_list). */
static constexpr size_t MAX_HTTPD_CLIENTS = 8;

/**
 * Кешированное количество WS-клиентов.
 * Обновляется в ws_handler (на подключении) и в RefreshWsClientList (на
 * отправке телеметрии и при явном опросе перед построением кадра); читается
 * атомарно без мьютексов httpd — безопасно вызывать из control loop.
 */
static std::atomic<uint8_t> s_cached_client_count{0};

/** Счётчик неудачных отправок для каждого fd — при 3 подряд закрываем. */
static constexpr int MAX_SEND_FAILURES = 3;

static WebSocketJsonHandler s_json_handler = nullptr;

void WebSocketSetJsonHandler(WebSocketJsonHandler handler) {
  s_json_handler = handler;
}

// Обработчик WebSocket: вызывается один раз на каждый фрейм (как в
// ws_echo_server). Цикл while(1) ломал порядок: фреймворк не успевал читать
// opcode следующего фрейма → "not properly masked".
static esp_err_t ws_handler(httpd_req_t* req) {
  if (req->method == HTTP_GET) {
    ESP_LOGI(TAG, "WebSocket connection request");
    // При WebSocket handshake клиент уже в списке httpd, но может ещё
    // не быть помечен как WS. Гарантируем count >= 1.
    int fds[MAX_HTTPD_CLIENTS];
    size_t cnt = MAX_HTTPD_CLIENTS;
    if (httpd_get_client_list(ws_server_handle, &cnt, fds) == ESP_OK) {
      ESP_LOGI(TAG, "httpd_get_client_list: %zu clients", cnt);
      uint8_t count = (cnt > 0) ? static_cast<uint8_t>(cnt) : 1;
      s_cached_client_count.store(count, std::memory_order_relaxed);
    } else {
      // Если список не получен, всё равно знаем что есть хотя бы 1 клиент
      s_cached_client_count.store(1, std::memory_order_relaxed);
    }
    return ESP_OK;
  }

  // Локальный буфер: не static, чтобы избежать гонки при нескольких
  // одновременных WebSocket-соединениях.
  uint8_t buf[kWsRxBufferSize];
  httpd_ws_frame_t ws_pkt = {};
  ws_pkt.payload = buf;
  ws_pkt.len = 0;

  esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, kWsRxBufferSize);
  if (ret != ESP_OK) {
    return ret;
  }

  if (ws_pkt.len == 0 || ws_pkt.type != HTTPD_WS_TYPE_TEXT) {
    return ESP_OK;
  }

  size_t safe_len = ws_pkt.len;
  if (safe_len >= kWsRxBufferSize) {
    safe_len = kWsRxBufferSize - 1;
  }
  buf[safe_len] = '\0';

  cJSON* json = cJSON_Parse(reinterpret_cast<char*>(ws_pkt.payload));
  if (json == NULL) {
    ESP_LOGW(TAG, "Failed to parse JSON");
    return ESP_OK;  // не рвём соединение из‑за битого кадра
  }

  cJSON* type = cJSON_GetObjectItem(json, "type");
  if (type && cJSON_IsString(type) && s_json_handler) {
    s_json_handler(type->valuestring, json, req);
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

  esp_err_t ret = httpd_register_uri_handler(ws_server_handle, &ws_uri);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "WebSocket URI /ws registered");
  } else {
    ESP_LOGE(TAG, "Failed to register WebSocket URI: %s", esp_err_to_name(ret));
  }
  return ret;
}

/**
 * Опросить httpd и обновить кеш числа WS-клиентов.
 *
 * Вызывается из задачи-отправителя телеметрии, не из control loop.
 * Считаются только сокеты, реально помеченные httpd как WebSocket:
 * обычные keep-alive HTTP-сессии (портал раздаётся с той же ESP) клиентами
 * телеметрии не являются.
 *
 * @param[out] ws_fds fd подключённых WS-клиентов (первые N элементов)
 * @return число WS-клиентов; 0 при ошибке опроса или отсутствии сервера
 */
static uint8_t RefreshWsClientList(int (&ws_fds)[MAX_HTTPD_CLIENTS]) {
  if (ws_server_handle == NULL) {
    s_cached_client_count.store(0, std::memory_order_relaxed);
    return 0;
  }

  int client_fds[MAX_HTTPD_CLIENTS];
  size_t client_count = MAX_HTTPD_CLIENTS;
  esp_err_t list_err =
      httpd_get_client_list(ws_server_handle, &client_count, client_fds);
  if (list_err != ESP_OK) {
    ESP_LOGW(TAG, "httpd_get_client_list failed: %s",
             esp_err_to_name(list_err));
    s_cached_client_count.store(0, std::memory_order_relaxed);
    return 0;
  }

  uint8_t ws_count = 0;
  for (size_t i = 0; i < client_count; i++) {
    int fd = client_fds[i];
    if (httpd_ws_get_fd_info(ws_server_handle, fd) !=
        HTTPD_WS_CLIENT_WEBSOCKET) {
      ESP_LOGD(TAG, "fd %d is not a WS client, skipping", fd);
      continue;
    }
    ws_fds[ws_count++] = fd;
  }

  s_cached_client_count.store(ws_count, std::memory_order_relaxed);
  return ws_count;
}

uint8_t WebSocketRefreshAndGetClientCount(void) {
  int ws_fds[MAX_HTTPD_CLIENTS];
  return RefreshWsClientList(ws_fds);
}

esp_err_t WebSocketSendTelem(const char* telem_json) {
  if (ws_server_handle == NULL || telem_json == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  // Заодно обновляем кеш для WebSocketGetClientCount().
  int ws_fds[MAX_HTTPD_CLIENTS];
  uint8_t ws_count = RefreshWsClientList(ws_fds);
  if (ws_count == 0) {
    return ESP_OK;
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

  // Счётчик последовательных ошибок на клиента (по fd) — чистая логика
  // вынесена в WsClientFailureTracker (firmware_common/ws_client_failure_
  // tracker.hpp) и покрыта host-тестами в projects/firmware_common/tests.
  static WsClientFailureTracker<kWsMaxClients> s_fd_tracker;

  for (uint8_t i = 0; i < ws_count; i++) {
    int fd = ws_fds[i];
    int slot = s_fd_tracker.FindOrAllocate(fd);

    esp_err_t send_err = httpd_ws_send_data(ws_server_handle, fd, &ws_pkt);
    if (send_err != ESP_OK) {
      int fails = s_fd_tracker.RecordFailure(slot);
      ESP_LOGW(TAG, "WS send failed fd=%d err=%s consecutive=%d", fd,
               esp_err_to_name(send_err), fails);
      if (fails >= MAX_SEND_FAILURES) {
        ESP_LOGW(TAG, "Closing stale WS client fd %d after %d failures", fd,
                 fails);
        httpd_sess_trigger_close(ws_server_handle, fd);
        s_fd_tracker.Evict(slot);
      }
    } else {
      s_fd_tracker.RecordSuccess(slot);
    }
  }

  s_fd_tracker.GarbageCollect(ws_fds, ws_count);

  return ESP_OK;
}

uint8_t WebSocketGetClientCount(void) {
  // Возвращаем кешированное значение — никаких мьютексов httpd, безопасно
  // вызывать из control loop на Core 1 без риска блокировки.
  return s_cached_client_count.load(std::memory_order_relaxed);
}

}  // namespace firmware_common::esp32
