#include "stabilization_config_nvs.hpp"

#include <cstring>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char* TAG = "stab_cfg_nvs";
static const char* NVS_NAMESPACE = "stab_cfg";
static const char* NVS_KEY = "config";

namespace stab_config_nvs {

esp_err_t Load(StabilizationConfig& config) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    if (err != ESP_ERR_NVS_NOT_FOUND) {
      ESP_LOGW(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
    }
    return err;
  }

  size_t required_size = sizeof(StabilizationConfig);
  err = nvs_get_blob(handle, NVS_KEY, &config, &required_size);
  nvs_close(handle);

  if (err == ESP_OK) {
    if (required_size == sizeof(StabilizationConfig) && config.IsValid()) {
      ESP_LOGI(TAG,
               "Loaded stabilization config: enabled=%d beta=%.3f "
               "lpf_cutoff=%.1f Hz mode=%d",
               config.enabled, config.madgwick_beta, config.lpf_cutoff_hz,
               config.mode);
      return ESP_OK;
    } else {
      ESP_LOGW(TAG, "Loaded config is invalid (size=%zu expected=%zu valid=%d)",
               required_size, sizeof(StabilizationConfig), config.IsValid());
      return ESP_ERR_INVALID_STATE;
    }
  } else if (err != ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(TAG, "Failed to read config from NVS: %s", esp_err_to_name(err));
  }

  return err;
}

esp_err_t Save(const StabilizationConfig& config) {
  if (!config.IsValid()) {
    ESP_LOGE(TAG, "Cannot save invalid config");
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS namespace for write: %s",
             esp_err_to_name(err));
    return err;
  }

  err = nvs_set_blob(handle, NVS_KEY, &config, sizeof(StabilizationConfig));
  if (err == ESP_OK) {
    err = nvs_commit(handle);
    if (err == ESP_OK) {
      ESP_LOGI(TAG,
               "Saved stabilization config: enabled=%d beta=%.3f "
               "lpf_cutoff=%.1f Hz mode=%d",
               config.enabled, config.madgwick_beta, config.lpf_cutoff_hz,
               config.mode);
    } else {
      ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
    }
  } else {
    ESP_LOGE(TAG, "Failed to write config to NVS: %s", esp_err_to_name(err));
  }

  nvs_close(handle);
  return err;
}

esp_err_t Erase() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to open NVS namespace for erase: %s",
             esp_err_to_name(err));
    return err;
  }

  err = nvs_erase_key(handle, NVS_KEY);
  if (err == ESP_OK) {
    err = nvs_commit(handle);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Erased stabilization config from NVS");
    }
  } else if (err == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGI(TAG, "No config to erase");
    err = ESP_OK;
  } else {
    ESP_LOGW(TAG, "Failed to erase config: %s", esp_err_to_name(err));
  }

  nvs_close(handle);
  return err;
}

}  // namespace stab_config_nvs