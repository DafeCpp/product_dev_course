#include "stabilization_config_nvs.hpp"

#include <cstring>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

using rc_vehicle::StabilizationConfig;

static const char* TAG = "stab_cfg_nvs";
static const char* NVS_NAMESPACE = "stab_cfg";
static const char* NVS_KEY = "config";

/** Текущая версия формата. Увеличивать при изменении StabilizationConfig. */
// v2: добавлены FilterConfig::adaptive_beta_enabled, adaptive_accel_threshold_g
// v3: добавлены slew_throttle, slew_steering в StabilizationConfig
// v4: добавлены FilterConfig::madgwick_enabled, ekf_enabled
// v5: добавлены KidsModeConfig::speed_limit_enabled, max_speed_ms, speed_limit_gain
// v6: добавлены StabilizationConfig::braking_mode, brake_slew_multiplier
static constexpr uint8_t kCurrentStabConfigVersion = 6;

/** Обёртка с версионным заголовком для NVS-хранения. */
struct StabConfigBlob {
  uint8_t version;
  uint8_t reserved[3];
  StabilizationConfig config;
};

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

  StabConfigBlob blob{};
  size_t required_size = sizeof(StabConfigBlob);
  err = nvs_get_blob(handle, NVS_KEY, &blob, &required_size);
  nvs_close(handle);

  if (err == ESP_OK) {
    if (required_size != sizeof(StabConfigBlob)) {
      ESP_LOGW(TAG, "Config size mismatch (got=%zu expected=%zu) — discarding",
               required_size, sizeof(StabConfigBlob));
      return ESP_ERR_NOT_FOUND;
    }
    if (blob.version != kCurrentStabConfigVersion) {
      ESP_LOGW(TAG, "Config version mismatch (got=%u expected=%u) — discarding",
               blob.version, kCurrentStabConfigVersion);
      return ESP_ERR_NOT_FOUND;
    }
    if (!blob.config.IsValid()) {
      ESP_LOGW(TAG, "Loaded config failed validation — discarding");
      return ESP_ERR_INVALID_STATE;
    }
    config = blob.config;
    config.Clamp();
    ESP_LOGI(TAG,
             "Loaded stabilization config: enabled=%d beta=%.3f "
             "lpf_cutoff=%.1f Hz mode=%d",
             config.enabled, config.filter.madgwick_beta,
             config.filter.lpf_cutoff_hz, static_cast<int>(config.mode));
    return ESP_OK;
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

  StabConfigBlob blob{};
  blob.version = kCurrentStabConfigVersion;
  blob.config = config;
  err = nvs_set_blob(handle, NVS_KEY, &blob, sizeof(StabConfigBlob));
  if (err == ESP_OK) {
    err = nvs_commit(handle);
    if (err == ESP_OK) {
      ESP_LOGI(TAG,
               "Saved stabilization config: enabled=%d beta=%.3f "
               "lpf_cutoff=%.1f Hz mode=%d",
               config.enabled, config.filter.madgwick_beta,
               config.filter.lpf_cutoff_hz, static_cast<int>(config.mode));
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