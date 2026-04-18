#include "websocket_server.hpp"

#include <atomic>
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
static constexpr size_t TELEM_BUF_SIZE = 2048;
/** Очередь длины 1: индекс буфера (0 или 1), готового к отправке. */
static QueueHandle_t s_telem_queue = NULL;
static char s_telem_buf[2][TELEM_BUF_SIZE];
// Index of the buffer that the *next* WebSocketEnqueueTelem() call will write
// into. Atomic to ensure the store to the buffer is visible to
// telem_sender_task on the other core before the index swap.
static std::atomic<uint8_t> s_telem_write_idx{0};

/**
 * Кешированное количество WS-клиентов.
 * Обновляется только в telem_sender_task (низкий приоритет, не блокирует
 * control loop). GetWebSocketClientCount() читает атомарно без мьютексов httpd.
 */
static std::atomic<uint8_t> s_cached_client_count{0};

/** Счётчик неудачных отправок для каждого fd — при 3 подряд закрываем. */
static constexpr int MAX_SEND_FAILURES = 3;

static void telem_sender_task(void* arg) {
  (void)arg;
  uint32_t frames_sent = 0;
  TickType_t last_diag = xTaskGetTickCount();
  for (;;) {
    uint8_t idx;
    if (xQueueReceive(s_telem_queue, &idx, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    WebSocketSendTelem(s_telem_buf[idx]);
    frames_sent++;

    // Диагностический лог каждые 10 секунд
    TickType_t now = xTaskGetTickCount();
    if ((now - last_diag) >= pdMS_TO_TICKS(10000)) {
      ESP_LOGI(TAG, "telem_sender: %lu frames sent in 10s, clients=%u",
               (unsigned long)frames_sent,
               (unsigned)s_cached_client_count.load(std::memory_order_relaxed));
      frames_sent = 0;
      last_diag = now;
    }
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
    // Обновить кеш при новом подключении
    int fds[WEBSOCKET_MAX_CLIENTS];
    size_t cnt = WEBSOCKET_MAX_CLIENTS;
    if (httpd_get_client_list(ws_server_handle, &cnt, fds) == ESP_OK) {
      s_cached_client_count.store(static_cast<uint8_t>(cnt), std::memory_order_relaxed);
    }
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
    ESP_LOGW(TAG, "Telem JSON truncated: %zu > %zu bytes", len, TELEM_BUF_SIZE);
    len = TELEM_BUF_SIZE - 1;
  }
  // Read current write index (relaxed — only this task writes it).
  const uint8_t idx = s_telem_write_idx.load(std::memory_order_relaxed);
  memcpy(s_telem_buf[idx], telem_json, len);
  s_telem_buf[idx][len] = '\0';
  // Swap write index with release: guarantees the memcpy above is visible to
  // telem_sender_task (on potentially different core) before it reads the buffer.
  s_telem_write_idx.store(1 - idx, std::memory_order_release);
  xQueueOverwrite(s_telem_queue, &idx);
}

esp_err_t WebSocketSendTelem(const char* telem_json) {
  if (ws_server_handle == NULL || telem_json == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  // Получить список клиентов (вызывается из telem_sender_task, не из control
  // loop). Заодно обновляем кеш для GetWebSocketClientCount().
  int client_fds[WEBSOCKET_MAX_CLIENTS];
  size_t client_count = WEBSOCKET_MAX_CLIENTS;
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
  s_cached_client_count.store(static_cast<uint8_t>(client_count), std::memory_order_relaxed);

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
  static int s_fd_fail_count[WEBSOCKET_MAX_CLIENTS] = {};
  static int s_fd_keys[WEBSOCKET_MAX_CLIENTS] = {};
  static bool s_fd_keys_initialized = false;
  if (!s_fd_keys_initialized) {
    for (int s = 0; s < WEBSOCKET_MAX_CLIENTS; s++) {
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
    for (int s = 0; s < WEBSOCKET_MAX_CLIENTS; s++) {
      if (s_fd_keys[s] == fd) {
        slot = s;
        break;
      }
    }
    if (slot == -1) {
      // Новый fd — занять свободный слот
      for (int s = 0; s < WEBSOCKET_MAX_CLIENTS; s++) {
        if (s_fd_keys[s] == -1) {
          s_fd_keys[s] = fd;
          s_fd_fail_count[s] = 0;
          slot = s;
          break;
        }
      }
    }

    esp_err_t send_err =
        httpd_ws_send_data(ws_server_handle, fd, &ws_pkt);
    if (send_err != ESP_OK) {
      if (slot >= 0) s_fd_fail_count[slot]++;
      int fails = (slot >= 0) ? s_fd_fail_count[slot] : -1;
      ESP_LOGW(TAG, "WS send failed fd=%d err=%s consecutive=%d",
               fd, esp_err_to_name(send_err), fails);
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
  for (int s = 0; s < WEBSOCKET_MAX_CLIENTS; s++) {
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
