#include "wifi_ap.hpp"

#include <stdio.h>
#include <string.h>

#include "config.hpp"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char* TAG = "wifi_ap";
static esp_netif_t* ap_netif = nullptr;
static esp_netif_t* sta_netif = nullptr;

static portMUX_TYPE s_wifi_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_inited = false;
static bool s_sta_should_connect = false;

static char s_ap_ssid[32] = {};
static WiFiStaStatus s_sta_status = {};

// STA reconnect backoff: после kStaFastRetries быстрых попыток переходим
// на медленный режим (раз в kStaSlowRetryMs), чтобы не мешать AP-клиентам.
static int s_sta_retry_count = 0;
static constexpr int kStaFastRetries = 3;
static constexpr uint32_t kStaSlowRetryMs = 30000;  // 30 сек
static esp_timer_handle_t s_sta_retry_timer = nullptr;

static void sta_retry_timer_cb(void*) {
  bool should = false;
  portENTER_CRITICAL(&s_wifi_mux);
  should = s_sta_should_connect;
  portEXIT_CRITICAL(&s_wifi_mux);
  if (should) {
    ESP_LOGI(TAG, "STA slow retry: attempting reconnect...");
    (void)esp_wifi_connect();
  }
}

static constexpr const char* kStaNvsNamespace = "wifi_sta";
static constexpr const char* kStaKeySsid = "ssid";
static constexpr const char* kStaKeyPass = "pass";

static void StaStatusSetConfigured(const char* ssid) {
  portENTER_CRITICAL(&s_wifi_mux);
  s_sta_status.configured = (ssid != nullptr && ssid[0] != '\0');
  if (ssid) {
    strncpy(s_sta_status.ssid, ssid, sizeof(s_sta_status.ssid) - 1);
    s_sta_status.ssid[sizeof(s_sta_status.ssid) - 1] = '\0';
  } else {
    s_sta_status.ssid[0] = '\0';
  }
  portEXIT_CRITICAL(&s_wifi_mux);
}

static void StaStatusSetConnected(bool connected) {
  portENTER_CRITICAL(&s_wifi_mux);
  s_sta_status.connected = connected;
  if (!connected) {
    s_sta_status.ip[0] = '\0';
  }
  portEXIT_CRITICAL(&s_wifi_mux);
}

static void StaStatusSetIp(const esp_netif_ip_info_t& ip_info) {
  portENTER_CRITICAL(&s_wifi_mux);
  snprintf(s_sta_status.ip, sizeof(s_sta_status.ip), IPSTR,
           IP2STR(&ip_info.ip));
  s_sta_status.connected = true;
  portEXIT_CRITICAL(&s_wifi_mux);
}

static void StaStatusSetDisconnectReason(int reason) {
  portENTER_CRITICAL(&s_wifi_mux);
  s_sta_status.last_disconnect_reason = reason;
  portEXIT_CRITICAL(&s_wifi_mux);
}

static bool LoadStaCreds(char* ssid, size_t ssid_len, char* pass,
                         size_t pass_len) {
  if (!ssid || ssid_len == 0 || !pass || pass_len == 0) return false;
  ssid[0] = '\0';
  pass[0] = '\0';

  nvs_handle_t h;
  esp_err_t e = nvs_open(kStaNvsNamespace, NVS_READONLY, &h);
  if (e != ESP_OK) return false;

  size_t need = 0;
  e = nvs_get_str(h, kStaKeySsid, nullptr, &need);
  if (e != ESP_OK || need == 0 || need > ssid_len) {
    nvs_close(h);
    return false;
  }
  e = nvs_get_str(h, kStaKeySsid, ssid, &need);
  if (e != ESP_OK || ssid[0] == '\0') {
    nvs_close(h);
    return false;
  }

  need = 0;
  e = nvs_get_str(h, kStaKeyPass, nullptr, &need);
  if (e == ESP_OK && need > 0 && need <= pass_len) {
    (void)nvs_get_str(h, kStaKeyPass, pass, &need);
  } else {
    pass[0] = '\0';
  }

  nvs_close(h);
  return true;
}

static esp_err_t SaveStaCreds(const char* ssid, const char* pass) {
  nvs_handle_t h;
  esp_err_t e = nvs_open(kStaNvsNamespace, NVS_READWRITE, &h);
  if (e != ESP_OK) return e;
  e = nvs_set_str(h, kStaKeySsid, ssid ? ssid : "");
  if (e == ESP_OK) e = nvs_set_str(h, kStaKeyPass, pass ? pass : "");
  if (e == ESP_OK) e = nvs_commit(h);
  nvs_close(h);
  return e;
}

static void ClearStaCreds() {
  nvs_handle_t h;
  if (nvs_open(kStaNvsNamespace, NVS_READWRITE, &h) != ESP_OK) return;
  (void)nvs_erase_key(h, kStaKeySsid);
  (void)nvs_erase_key(h, kStaKeyPass);
  (void)nvs_commit(h);
  nvs_close(h);
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
  (void)arg;
  (void)event_data;

  if (event_base == WIFI_EVENT) {
    if (event_id == WIFI_EVENT_STA_START) {
      bool should_connect = false;
      portENTER_CRITICAL(&s_wifi_mux);
      should_connect = s_sta_should_connect;
      portEXIT_CRITICAL(&s_wifi_mux);
      if (should_connect) {
        ESP_LOGI(TAG, "STA start → connecting...");
        (void)esp_wifi_connect();
      }
      return;
    }

    if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
      const auto* disc =
          reinterpret_cast<const wifi_event_sta_disconnected_t*>(event_data);
      const int reason = disc ? (int)disc->reason : 0;
      StaStatusSetConnected(false);
      StaStatusSetDisconnectReason(reason);

      bool should_connect = false;
      portENTER_CRITICAL(&s_wifi_mux);
      should_connect = s_sta_should_connect;
      portEXIT_CRITICAL(&s_wifi_mux);

      if (should_connect) {
        ++s_sta_retry_count;

        // reason=201 (NO_AP_FOUND): сеть не в радиусе — быстрые ретраи
        // бессмысленны, сканирование блокирует радио на сотни мс и мешает
        // AP-клиентам.
        const bool skip_fast = (reason == 201);

        if (!skip_fast && s_sta_retry_count <= kStaFastRetries) {
          ESP_LOGW(TAG, "STA disconnected (reason=%d), retry %d/%d", reason,
                   s_sta_retry_count, kStaFastRetries);
          (void)esp_wifi_connect();
        } else {
          if (s_sta_retry_count <= kStaFastRetries + 1) {
            ESP_LOGW(TAG,
                     "STA disconnected (reason=%d), slow retry every %lu s",
                     reason, (unsigned long)(kStaSlowRetryMs / 1000));
          }
          if (s_sta_retry_timer) {
            esp_timer_start_once(s_sta_retry_timer, kStaSlowRetryMs * 1000ULL);
          }
        }
      } else {
        ESP_LOGI(TAG, "STA disconnected (reason=%d), not reconnecting", reason);
      }
      return;
    }
  }

  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    const auto* evt = reinterpret_cast<const ip_event_got_ip_t*>(event_data);
    if (evt) {
      StaStatusSetIp(evt->ip_info);
      s_sta_retry_count = 0;  // Сброс: подключились успешно
      if (s_sta_retry_timer) esp_timer_stop(s_sta_retry_timer);
      ESP_LOGI(TAG, "STA got IP: " IPSTR, IP2STR(&evt->ip_info.ip));
    }
    return;
  }
}

esp_err_t WiFiApInit(void) {
  if (s_inited) return ESP_OK;

  // Инициализация NVS (нужно для Wi-Fi)
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Инициализация сетевого интерфейса и цикла событий (порядок как в softAP
  // example)
  ESP_ERROR_CHECK(esp_netif_init());
  ret = esp_event_loop_create_default();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    return ret;
  }
  ap_netif = esp_netif_create_default_wifi_ap();
  sta_netif = esp_netif_create_default_wifi_sta();

  // Конфигурация Wi-Fi
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  // События Wi‑Fi / IP (для STA статуса)
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &wifi_event_handler, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &wifi_event_handler, nullptr));

  // Получить MAC адрес для уникального SSID
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s-%02X%02X", WIFI_AP_SSID_PREFIX,
           mac[4], mac[5]);

  // Настройка AP
  wifi_config_t ap_cfg = {};
  strncpy((char*)ap_cfg.ap.ssid, s_ap_ssid, sizeof(ap_cfg.ap.ssid) - 1);
  ap_cfg.ap.ssid_len = strlen(s_ap_ssid);
  if (strlen(WIFI_AP_PASSWORD) > 0) {
    strncpy((char*)ap_cfg.ap.password, WIFI_AP_PASSWORD,
            sizeof(ap_cfg.ap.password) - 1);
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
  } else {
    ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
  }
  ap_cfg.ap.channel = WIFI_AP_CHANNEL;
  ap_cfg.ap.max_connection = WIFI_AP_MAX_CONNECTIONS;
  ap_cfg.ap.beacon_interval = 100;

  // TODO: одноразовая очистка мусорных STA creds из NVS (удалить после прошивки)
  ClearStaCreds();
  ESP_LOGW(TAG, "STA creds cleared (one-time cleanup)");

  // Настройка STA (опционально): пробуем загрузить из NVS и подключиться.
  char sta_ssid[33];
  char sta_pass[65];
  bool have_sta =
      LoadStaCreds(sta_ssid, sizeof(sta_ssid), sta_pass, sizeof(sta_pass));

  if (have_sta) {
    ESP_LOGI(TAG, "NVS STA creds: SSID=[%s] (len=%d), pass_len=%d",
             sta_ssid, (int)strlen(sta_ssid), (int)strlen(sta_pass));
  } else {
    ESP_LOGI(TAG, "No STA creds in NVS");
  }

  wifi_config_t sta_cfg = {};
  if (have_sta) {
    strncpy((char*)sta_cfg.sta.ssid, sta_ssid, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char*)sta_cfg.sta.password, sta_pass,
            sizeof(sta_cfg.sta.password) - 1);
    StaStatusSetConfigured(sta_ssid);
    portENTER_CRITICAL(&s_wifi_mux);
    s_sta_should_connect = true;
    portEXIT_CRITICAL(&s_wifi_mux);
  } else {
    StaStatusSetConfigured(nullptr);
    portENTER_CRITICAL(&s_wifi_mux);
    s_sta_should_connect = false;
    portEXIT_CRITICAL(&s_wifi_mux);
  }

  // AP + STA одновременно (точка доступа остаётся поднятой).
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
  if (have_sta) {
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
  }
  ESP_ERROR_CHECK(esp_wifi_start());

  // Таймер для медленных STA-ретраев (esp_timer — свой таск с достаточным
  // стеком)
  const esp_timer_create_args_t retry_timer_args = {
      .callback = sta_retry_timer_cb,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "sta_retry",
      .skip_unhandled_events = true,
  };
  esp_timer_create(&retry_timer_args, &s_sta_retry_timer);

  ESP_LOGI(TAG, "Wi-Fi AP initialized. SSID: %s", s_ap_ssid);
  if (have_sta) {
    ESP_LOGI(TAG, "STA configured. SSID: %s (connecting...)", sta_ssid);
  } else {
    ESP_LOGI(TAG, "STA not configured (use web UI to connect)");
  }

  s_inited = true;
  return ESP_OK;
}

esp_err_t WiFiApGetSsid(char* ssid_str, size_t len) {
  if (!ssid_str || len == 0) return ESP_ERR_INVALID_ARG;
  ssid_str[0] = '\0';

  portENTER_CRITICAL(&s_wifi_mux);
  strncpy(ssid_str, s_ap_ssid, len - 1);
  ssid_str[len - 1] = '\0';
  portEXIT_CRITICAL(&s_wifi_mux);
  return ESP_OK;
}

esp_err_t WiFiApGetIp(char* ip_str, size_t len) {
  if (ap_netif == nullptr || ip_str == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_netif_ip_info_t ip_info;
  if (esp_netif_get_ip_info(ap_netif, &ip_info) != ESP_OK) {
    return ESP_FAIL;
  }

  snprintf(ip_str, len, IPSTR, IP2STR(&ip_info.ip));
  return ESP_OK;
}

esp_err_t WiFiStaConnect(const char* ssid, const char* password, bool save) {
  if (!ssid || ssid[0] == '\0') {
    return ESP_ERR_INVALID_ARG;
  }

  // Ограничения Wi‑Fi: SSID <= 32, password <= 64 (плюс '\0')
  if (strlen(ssid) > 32) return ESP_ERR_INVALID_SIZE;
  if (password && strlen(password) > 64) return ESP_ERR_INVALID_SIZE;

  wifi_config_t sta_cfg = {};
  strncpy((char*)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
  if (password) {
    strncpy((char*)sta_cfg.sta.password, password,
            sizeof(sta_cfg.sta.password) - 1);
  }

  if (save) {
    esp_err_t e = SaveStaCreds(ssid, password ? password : "");
    if (e != ESP_OK) {
      ESP_LOGW(TAG, "Failed to save STA creds to NVS: %s", esp_err_to_name(e));
    }
  }

  StaStatusSetConfigured(ssid);
  StaStatusSetConnected(false);

  portENTER_CRITICAL(&s_wifi_mux);
  s_sta_should_connect = true;
  portEXIT_CRITICAL(&s_wifi_mux);

  s_sta_retry_count = 0;  // Сброс backoff при ручном подключении
  if (s_sta_retry_timer) esp_timer_stop(s_sta_retry_timer);

  // Обновить конфиг и начать подключение (AP остаётся включён).
  esp_err_t e = esp_wifi_set_mode(WIFI_MODE_APSTA);
  if (e != ESP_OK) return e;
  e = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
  if (e != ESP_OK) return e;

  (void)esp_wifi_disconnect();
  e = esp_wifi_connect();
  return e;
}

esp_err_t WiFiStaDisconnect(bool forget) {
  portENTER_CRITICAL(&s_wifi_mux);
  s_sta_should_connect = false;
  portEXIT_CRITICAL(&s_wifi_mux);

  StaStatusSetConnected(false);
  if (forget) {
    ClearStaCreds();
    StaStatusSetConfigured(nullptr);
  }

  // Отключаемся только от STA; AP остаётся.
  return esp_wifi_disconnect();
}

esp_err_t WiFiStaGetStatus(WiFiStaStatus* out_status) {
  if (!out_status) return ESP_ERR_INVALID_ARG;

  portENTER_CRITICAL(&s_wifi_mux);
  *out_status = s_sta_status;
  portEXIT_CRITICAL(&s_wifi_mux);

  // RSSI (если подключены)
  if (out_status->connected) {
    wifi_ap_record_t ap_info = {};
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
      out_status->rssi = (int)ap_info.rssi;
    }
  }
  return ESP_OK;
}

esp_err_t WiFiStaScan(WiFiScanNetwork* out_networks, size_t* inout_count) {
  if (!out_networks || !inout_count) return ESP_ERR_INVALID_ARG;
  if (*inout_count == 0) return ESP_ERR_INVALID_ARG;
  if (!s_inited) return ESP_ERR_INVALID_STATE;

  // Примечание: скан может занять несколько секунд и временно ухудшить связь
  // AP, т.к. радио сканирует другие каналы.
  wifi_scan_config_t scan_cfg = {};
  scan_cfg.show_hidden = true;
  scan_cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  scan_cfg.scan_time.active.min = 60;
  scan_cfg.scan_time.active.max = 150;

  esp_err_t e = esp_wifi_scan_start(&scan_cfg, true /* block */);
  if (e != ESP_OK) {
    ESP_LOGW(TAG, "WiFi scan failed: %s", esp_err_to_name(e));
    return e;
  }

  uint16_t ap_num = 0;
  (void)esp_wifi_scan_get_ap_num(&ap_num);

  constexpr uint16_t kMaxRecords = 20;
  wifi_ap_record_t records[kMaxRecords];
  memset(records, 0, sizeof(records));

  uint16_t fetch = ap_num;
  if (fetch > kMaxRecords) fetch = kMaxRecords;
  e = esp_wifi_scan_get_ap_records(&fetch, records);
  if (e != ESP_OK) {
    return e;
  }

  const size_t capacity = *inout_count;
  size_t out_count = 0;

  // Dedup по SSID: оставляем самый сильный RSSI на SSID.
  for (uint16_t i = 0; i < fetch; i++) {
    const char* ssid = reinterpret_cast<const char*>(records[i].ssid);
    if (!ssid || ssid[0] == '\0') continue;

    size_t existing = capacity;
    for (size_t j = 0; j < out_count; j++) {
      if (strcmp(out_networks[j].ssid, ssid) == 0) {
        existing = j;
        break;
      }
    }

    if (existing < out_count) {
      if ((int)records[i].rssi > out_networks[existing].rssi) {
        out_networks[existing].rssi = (int)records[i].rssi;
        out_networks[existing].channel = (int)records[i].primary;
        out_networks[existing].authmode = (int)records[i].authmode;
      }
      continue;
    }

    if (out_count >= capacity) break;

    out_networks[out_count].rssi = (int)records[i].rssi;
    out_networks[out_count].channel = (int)records[i].primary;
    out_networks[out_count].authmode = (int)records[i].authmode;
    strncpy(out_networks[out_count].ssid, ssid,
            sizeof(out_networks[out_count].ssid) - 1);
    out_networks[out_count].ssid[sizeof(out_networks[out_count].ssid) - 1] =
        '\0';
    out_count++;
  }

  *inout_count = out_count;
  return ESP_OK;
}
