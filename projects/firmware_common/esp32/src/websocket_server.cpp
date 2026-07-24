#include <string.h>

#include <atomic>
#include <firmware_common/esp32/websocket_server.hpp>

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
 * Обновляется в ws_handler (на подключении) и WebSocketSendTelem (на
 * отправке); читается атомарно без мьютексов httpd — безопасно вызывать из
 * control loop.
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

esp_err_t WebSocketSendTelem(const char* telem_json) {
  if (ws_server_handle == NULL || telem_json == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  // Получить список клиентов (вызывается из задачи-отправителя телеметрии, не
  // из control loop). Заодно обновляем кеш для WebSocketGetClientCount().
  int client_fds[MAX_HTTPD_CLIENTS];
  size_t client_count = MAX_HTTPD_CLIENTS;
  esp_err_t list_err =
      httpd_get_client_list(ws_server_handle, &client_count, client_fds);
  if (list_err != ESP_OK) {
    ESP_LOGW(TAG, "httpd_get_client_list failed: %s",
             esp_err_to_name(list_err));
    s_cached_client_count.store(0, std::memory_order_relaxed);
    return ESP_OK;
  }
  if (client_count == 0) {
    s_cached_client_count.store(0, std::memory_order_relaxed);
    return ESP_OK;
  }
  s_cached_client_count.store(static_cast<uint8_t>(client_count),
                              std::memory_order_relaxed);

  size_t len = strlen(telem_json);
  httpd_ws_frame_t ws_pkt = {};
  ws_pkt.final = true;
  ws_pkt.fragmented = false;
  ws_pkt.type = HTTPD_WS_TYPE_TEXT;
  // ESP-IDF API принимает uint8_t*, но при отправке данные не модифицирует.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  ws_pkt.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(telem_json));
  ws_pkt.len = len;

  // Счётчик последовательных ошибок: ключ — fd (не позиция в списке).
  // Позиция fd в массиве client_fds меняется между вызовами, поэтому
  // индексирование по i было бы некорректным.
  // Sentinel -1 means "empty slot". Using 0 would be incorrect because
  // fd 0 (stdin) is a valid file descriptor that httpd could reuse.
  static int s_fd_fail_count[kWsMaxClients] = {};
  static int s_fd_keys[kWsMaxClients] = {};
  static bool s_fd_keys_initialized = false;
  if (!s_fd_keys_initialized) {
    for (int s = 0; s < kWsMaxClients; s++) {
      s_fd_keys[s] = -1;
    }
    s_fd_keys_initialized = true;
  }

  for (size_t i = 0; i < client_count; i++) {
    int fd = client_fds[i];
    if (httpd_ws_get_fd_info(ws_server_handle, fd) !=
        HTTPD_WS_CLIENT_WEBSOCKET) {
      ESP_LOGD(TAG, "fd %d is not a WS client, skipping", fd);
      continue;
    }

    // Найти или выделить слот для этого fd
    int slot = -1;
    for (int s = 0; s < kWsMaxClients; s++) {
      if (s_fd_keys[s] == fd) {
        slot = s;
        break;
      }
    }
    if (slot == -1) {
      // Новый fd — занять свободный слот
      for (int s = 0; s < kWsMaxClients; s++) {
        if (s_fd_keys[s] == -1) {
          s_fd_keys[s] = fd;
          s_fd_fail_count[s] = 0;
          slot = s;
          break;
        }
      }
    }

    esp_err_t send_err = httpd_ws_send_data(ws_server_handle, fd, &ws_pkt);
    if (send_err != ESP_OK) {
      if (slot >= 0) s_fd_fail_count[slot]++;
      int fails = (slot >= 0) ? s_fd_fail_count[slot] : -1;
      ESP_LOGW(TAG, "WS send failed fd=%d err=%s consecutive=%d", fd,
               esp_err_to_name(send_err), fails);
      if (slot >= 0 && s_fd_fail_count[slot] >= MAX_SEND_FAILURES) {
        ESP_LOGW(TAG, "Closing stale WS client fd %d after %d failures", fd,
                 s_fd_fail_count[slot]);
        httpd_sess_trigger_close(ws_server_handle, fd);
        s_fd_keys[slot] = -1;
        s_fd_fail_count[slot] = 0;
      }
    } else {
      if (slot >= 0) s_fd_fail_count[slot] = 0;
    }
  }

  // Очистить слоты для fd, которых больше нет в списке клиентов
  for (int s = 0; s < kWsMaxClients; s++) {
    if (s_fd_keys[s] == -1) continue;
    bool found = false;
    for (size_t i = 0; i < client_count; i++) {
      if (client_fds[i] == s_fd_keys[s]) {
        found = true;
        break;
      }
    }
    if (!found) {
      ESP_LOGD(TAG, "fd %d left, clearing fail slot", s_fd_keys[s]);
      s_fd_keys[s] = -1;
      s_fd_fail_count[s] = 0;
    }
  }

  return ESP_OK;
}

uint8_t WebSocketGetClientCount(void) {
  // Возвращаем кешированное значение — никаких мьютексов httpd, безопасно
  // вызывать из control loop на Core 1 без риска блокировки.
  return s_cached_client_count.load(std::memory_order_relaxed);
}

}  // namespace firmware_common::esp32
