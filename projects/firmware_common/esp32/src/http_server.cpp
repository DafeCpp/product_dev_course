#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <firmware_common/esp32/http_server.hpp>
#include <firmware_common/esp32/wifi_ap.hpp>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"

namespace firmware_common::esp32 {

static const char* TAG = "http_server";
static httpd_handle_t server_handle = NULL;

httpd_handle_t HttpServerGetHandle(void) { return server_handle; }

static esp_err_t SendWifiStatusJson(httpd_req_t* req) {
  char ap_ip[16] = {};
  char ap_ssid[32] = {};
  (void)WiFiApGetIp(ap_ip, sizeof(ap_ip));
  (void)WiFiApGetSsid(ap_ssid, sizeof(ap_ssid));

  WiFiStaStatus sta = {};
  (void)WiFiStaGetStatus(&sta);

  cJSON* root = cJSON_CreateObject();
  if (!root) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Failed to allocate JSON");
    return ESP_FAIL;
  }

  cJSON* ap = cJSON_CreateObject();
  if (ap) {
    cJSON_AddStringToObject(ap, "ssid", ap_ssid);
    cJSON_AddStringToObject(ap, "ip", ap_ip);
    cJSON_AddItemToObject(root, "ap", ap);
  }

  cJSON* sta_obj = cJSON_CreateObject();
  if (sta_obj) {
    cJSON_AddBoolToObject(sta_obj, "configured", sta.configured);
    cJSON_AddBoolToObject(sta_obj, "connected", sta.connected);
    cJSON_AddNumberToObject(sta_obj, "reason", sta.last_disconnect_reason);
    cJSON_AddNumberToObject(sta_obj, "rssi", sta.rssi);
    cJSON_AddStringToObject(sta_obj, "ssid", sta.ssid);
    cJSON_AddStringToObject(sta_obj, "ip", sta.ip);
    cJSON_AddItemToObject(root, "sta", sta_obj);
  }

  char* json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!json_str) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Failed to render JSON");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
  free(json_str);
  return ESP_OK;
}

static esp_err_t wifi_status_handler(httpd_req_t* req) {
  if (req->method != HTTP_GET) {
    httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "GET only");
    return ESP_FAIL;
  }
  return SendWifiStatusJson(req);
}

static esp_err_t ReadJsonBody(httpd_req_t* req, char* buf, size_t buf_len) {
  if (!req || !buf || buf_len == 0) return ESP_ERR_INVALID_ARG;
  buf[0] = '\0';

  size_t total_len = req->content_len;
  if (total_len == 0) return ESP_OK;
  if (total_len >= buf_len) return ESP_ERR_INVALID_SIZE;

  size_t cur = 0;
  while (cur < total_len) {
    int ret = httpd_req_recv(req, buf + cur, total_len - cur);
    if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
      continue;
    }
    if (ret <= 0) {
      return ESP_FAIL;
    }
    cur += (size_t)ret;
  }
  buf[cur] = '\0';
  return ESP_OK;
}

static esp_err_t wifi_sta_connect_handler(httpd_req_t* req) {
  char body[256];
  esp_err_t e = ReadJsonBody(req, body, sizeof(body));
  if (e != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
    return ESP_FAIL;
  }

  cJSON* json = cJSON_Parse(body);
  if (!json) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_FAIL;
  }

  const cJSON* ssid = cJSON_GetObjectItem(json, "ssid");
  const cJSON* password = cJSON_GetObjectItem(json, "password");
  const cJSON* save = cJSON_GetObjectItem(json, "save");

  const char* ssid_str =
      (ssid && cJSON_IsString(ssid)) ? ssid->valuestring : "";
  const char* pass_str =
      (password && cJSON_IsString(password)) ? password->valuestring : "";
  bool save_cfg = true;
  if (save && cJSON_IsBool(save)) save_cfg = cJSON_IsTrue(save);

  (void)WiFiStaConnect(ssid_str, pass_str, save_cfg);
  cJSON_Delete(json);

  return SendWifiStatusJson(req);
}

static esp_err_t wifi_sta_disconnect_handler(httpd_req_t* req) {
  char body[128];
  bool forget = false;
  if (ReadJsonBody(req, body, sizeof(body)) == ESP_OK && body[0] != '\0') {
    cJSON* json = cJSON_Parse(body);
    if (json) {
      const cJSON* f = cJSON_GetObjectItem(json, "forget");
      if (f && cJSON_IsBool(f)) forget = cJSON_IsTrue(f);
      cJSON_Delete(json);
    }
  }

  (void)WiFiStaDisconnect(forget);
  return SendWifiStatusJson(req);
}

static esp_err_t wifi_scan_handler(httpd_req_t* req) {
  if (req->method != HTTP_GET) {
    httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "GET only");
    return ESP_FAIL;
  }

  WiFiScanNetwork nets[20] = {};
  size_t count = sizeof(nets) / sizeof(nets[0]);
  esp_err_t e = WiFiStaScan(nets, &count);
  if (e != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Scan failed");
    return ESP_FAIL;
  }

  cJSON* root = cJSON_CreateObject();
  if (!root) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Failed to allocate JSON");
    return ESP_FAIL;
  }

  cJSON* arr = cJSON_CreateArray();
  if (!arr) {
    cJSON_Delete(root);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Failed to allocate JSON");
    return ESP_FAIL;
  }
  cJSON_AddItemToObject(root, "networks", arr);

  for (size_t i = 0; i < count; i++) {
    if (nets[i].ssid[0] == '\0') continue;
    cJSON* n = cJSON_CreateObject();
    if (!n) continue;
    cJSON_AddStringToObject(n, "ssid", nets[i].ssid);
    cJSON_AddNumberToObject(n, "rssi", nets[i].rssi);
    cJSON_AddNumberToObject(n, "channel", nets[i].channel);
    cJSON_AddNumberToObject(n, "authmode", nets[i].authmode);
    cJSON_AddBoolToObject(n, "open", nets[i].authmode == 0);
    cJSON_AddItemToArray(arr, n);
  }

  char* json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!json_str) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Failed to render JSON");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
  free(json_str);
  return ESP_OK;
}

static esp_err_t redirect_to_root_handler(httpd_req_t* req) {
  char ap_ip[16] = {};
  char location[64] = {};
  if (WiFiApGetIp(ap_ip, sizeof(ap_ip)) == ESP_OK && ap_ip[0] != '\0') {
    snprintf(location, sizeof(location), "http://%s/", ap_ip);
  } else {
    strncpy(location, "http://192.168.4.1/", sizeof(location) - 1);
    location[sizeof(location) - 1] = '\0';
  }

  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", location);
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_send(req, "Redirecting to captive portal", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static void RegisterWifiApiRoutes(httpd_handle_t server) {
  httpd_uri_t wifi_status_uri = {
      .uri = "/api/wifi/status",
      .method = HTTP_GET,
      .handler = wifi_status_handler,
      .user_ctx = NULL,
#if CONFIG_HTTPD_WS_SUPPORT
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
#endif
  };
  httpd_register_uri_handler(server, &wifi_status_uri);

  httpd_uri_t wifi_connect_uri = {
      .uri = "/api/wifi/sta/connect",
      .method = HTTP_POST,
      .handler = wifi_sta_connect_handler,
      .user_ctx = NULL,
#if CONFIG_HTTPD_WS_SUPPORT
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
#endif
  };
  httpd_register_uri_handler(server, &wifi_connect_uri);

  httpd_uri_t wifi_disconnect_uri = {
      .uri = "/api/wifi/sta/disconnect",
      .method = HTTP_POST,
      .handler = wifi_sta_disconnect_handler,
      .user_ctx = NULL,
#if CONFIG_HTTPD_WS_SUPPORT
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
#endif
  };
  httpd_register_uri_handler(server, &wifi_disconnect_uri);

  httpd_uri_t wifi_scan_uri = {
      .uri = "/api/wifi/scan",
      .method = HTTP_GET,
      .handler = wifi_scan_handler,
      .user_ctx = NULL,
#if CONFIG_HTTPD_WS_SUPPORT
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
#endif
  };
  httpd_register_uri_handler(server, &wifi_scan_uri);
}

static void RegisterCaptivePortalRoutes(httpd_handle_t server) {
  // Captive portal probes (iOS/Android/Windows/macOS).
  httpd_uri_t captive_android_uri = {
      .uri = "/generate_204",
      .method = HTTP_GET,
      .handler = redirect_to_root_handler,
      .user_ctx = NULL,
#if CONFIG_HTTPD_WS_SUPPORT
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
#endif
  };
  httpd_register_uri_handler(server, &captive_android_uri);

  httpd_uri_t captive_android_alt_uri = {
      .uri = "/gen_204",
      .method = HTTP_GET,
      .handler = redirect_to_root_handler,
      .user_ctx = NULL,
#if CONFIG_HTTPD_WS_SUPPORT
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
#endif
  };
  httpd_register_uri_handler(server, &captive_android_alt_uri);

  httpd_uri_t captive_apple_uri = {
      .uri = "/hotspot-detect.html",
      .method = HTTP_GET,
      .handler = redirect_to_root_handler,
      .user_ctx = NULL,
#if CONFIG_HTTPD_WS_SUPPORT
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
#endif
  };
  httpd_register_uri_handler(server, &captive_apple_uri);

  httpd_uri_t captive_windows_uri = {
      .uri = "/ncsi.txt",
      .method = HTTP_GET,
      .handler = redirect_to_root_handler,
      .user_ctx = NULL,
#if CONFIG_HTTPD_WS_SUPPORT
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
#endif
  };
  httpd_register_uri_handler(server, &captive_windows_uri);

  httpd_uri_t captive_windows_alt_uri = {
      .uri = "/connecttest.txt",
      .method = HTTP_GET,
      .handler = redirect_to_root_handler,
      .user_ctx = NULL,
#if CONFIG_HTTPD_WS_SUPPORT
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
#endif
  };
  httpd_register_uri_handler(server, &captive_windows_alt_uri);

  httpd_uri_t captive_redirect_uri = {
      .uri = "/redirect",
      .method = HTTP_GET,
      .handler = redirect_to_root_handler,
      .user_ctx = NULL,
#if CONFIG_HTTPD_WS_SUPPORT
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL,
#endif
  };
  httpd_register_uri_handler(server, &captive_redirect_uri);
}

esp_err_t HttpServerInit(const HttpServerConfig& cfg) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = cfg.port;
  config.max_uri_handlers = cfg.max_uri_handlers;
  config.stack_size = cfg.stack_size;
  // Достаточно для 1 WS + несколько HTTP; httpd использует ещё 2 внутренних.
  config.max_open_sockets = cfg.max_open_sockets;
  // Секунды — мобильный клиент может быть медленнее.
  config.recv_wait_timeout = cfg.recv_wait_timeout_s;
  // Короткий тайм-аут: зависший send не должен блокировать httpd task и
  // telem_sender_task.
  config.send_wait_timeout = cfg.send_wait_timeout_s;
  config.lru_purge_enable =
      true;  // Автозакрытие старых соединений при нехватке
  config.uri_match_fn = httpd_uri_match_wildcard;

  ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);

  if (httpd_start(&server_handle, &config) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start HTTP server");
    return ESP_FAIL;
  }

  if (cfg.enable_wifi_api) {
    RegisterWifiApiRoutes(server_handle);
  }
  if (cfg.enable_captive_portal) {
    RegisterCaptivePortalRoutes(server_handle);
  }

  ESP_LOGI(TAG, "HTTP server started");
  return ESP_OK;
}

}  // namespace firmware_common::esp32
