#include "wifi_ap.hpp"

#include "config.hpp"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char* TAG = "wifi_ap";
static esp_netif_t* ap_netif = nullptr;

esp_err_t WiFiApInit(void) {
  // Инициализация NVS (нужно для Wi-Fi)
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Инициализация сетевого интерфейса
  ESP_ERROR_CHECK(esp_netif_init());
  ap_netif = esp_netif_create_default_wifi_ap();

  // Конфигурация Wi-Fi
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  // Получить MAC адрес для уникального SSID
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  char ssid[32];
  snprintf(ssid, sizeof(ssid), "%s-%02X%02X", WIFI_AP_SSID_PREFIX, mac[4],
           mac[5]);

  // Настройка AP
  wifi_config_t wifi_config = {};
  strncpy((char*)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid) - 1);
  wifi_config.ap.ssid_len = strlen(ssid);
  if (strlen(WIFI_AP_PASSWORD) > 0) {
    strncpy((char*)wifi_config.ap.password, WIFI_AP_PASSWORD,
            sizeof(wifi_config.ap.password) - 1);
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
  } else {
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
  }
  wifi_config.ap.channel = WIFI_AP_CHANNEL;
  wifi_config.ap.max_connection = WIFI_AP_MAX_CONNECTIONS;
  wifi_config.ap.beacon_interval = 100;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "Wi-Fi AP initialized. SSID: %s", ssid);
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
